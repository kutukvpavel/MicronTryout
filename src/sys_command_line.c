/**
  ******************************************************************************
  * @file:      sys_command_line.c
  * @author:    Cat
  * @author: 	Morgan Diepart
  * @version:   V1.0
  * @date:      2022-08-24
  * @brief:     command line
  * 
  ******************************************************************************
  */

#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <mik32_hal.h>
#include "sys_command_line.h"

#define EXIT_SUCCESS HAL_OK
#define EXIT_FAILURE HAL_ERROR

/*******************************************************************************
 *
 * 	Typedefs
 *
 ******************************************************************************/


/*
 * Buffer for current line
 */
typedef struct {
    uint8_t buff[MAX_LINE_LEN];
    uint8_t len;
} HANDLE_TYPE_S;

/*
 * Command entry
 */
typedef struct {
    const char *pCmd;
    const char *pHelp;
    uint8_t (*pFun)(int argc, char *argv[]);
} COMMAND_S;

/*
 * Command line history
 */
typedef struct {
    char cmd[HISTORY_MAX][MAX_LINE_LEN];
    uint8_t count;
    uint8_t latest;
    uint8_t show;
}HISTORY_S;

/*******************************************************************************
 *
 * 	Internal variables
 *
 ******************************************************************************/

shell_queue_s 			cli_rx_buff; 				/* 64 bytes FIFO, saving commands from the terminal */
COMMAND_S				CLI_commands[MAX_COMMAND_NB];
static HISTORY_S 		history;
char *cli_logs_names[] = {"SHELL",
#ifdef CLI_ADDITIONAL_LOG_CATEGORIES
#define X(name, b) #name,
		CLI_ADDITIONAL_LOG_CATEGORIES
#undef X
#endif
};

uint32_t cli_log_stat = 0
#ifdef CLI_ADDITIONAL_LOG_CATEGORIES
#define X(name, b) | (b<<CLI_LOG_##name)
		CLI_ADDITIONAL_LOG_CATEGORIES
#undef X
#endif
;

const char 				cli_help_help[] 			= "show commands";
const char 				cli_clear_help[] 			= "clear the screen";
const char 				cli_reset_help[] 			= "reboot MCU";
const char				cli_log_help[]				= "Controls which logs are displayed."
													  "\n\t\"log show\" to show which logs are enabled"
													  "\n\t\"log on/off all\" to enable/disable all logs"
													  "\n\t\"log on/off [CAT1 CAT2 CAT...]\" to enable/disable the logs for categories [CAT1 CAT2 CAT...]";
bool 					cli_password_ok 			= false;

/*******************************************************************************
 *
 * 	Internal functions declaration
 *
 ******************************************************************************/

static void 	cli_history_add			(char* buff);
static uint8_t 	cli_history_show		(uint8_t mode, char** p_history);
static void 	cli_rx_handle			(shell_queue_s *rx_buff);
uint8_t 		cli_help				(int argc, char *argv[]);
uint8_t 		cli_clear				(int argc, char *argv[]);
uint8_t 		cli_reset				(int argc, char *argv[]);
uint8_t 		cli_log					(int argc, char *argv[]);
void 			cli_add_command			(const char *command, const char *help, uint8_t (*exec)(int argc, char *argv[]));
void 			greet					(void);
void 			cli_disable_log_entry	(char *str);
void 			cli_enable_log_entry	(char *str);

__attribute__((weak)) int _isatty(int file){
	switch(file){
	case STDERR_FILENO:
	case STDIN_FILENO:
	case STDOUT_FILENO:
		return 1;
	default:
		errno = EBADF;
		return 0;
	}
}

/*******************************************************************************
 *
 * 	Functions definitions
 *
 ******************************************************************************/

/**
  * @brief          add a command to the history
  * @param  buff:   command
  * @retval         null
  */
static void cli_history_add(char* buff)
{
    uint16_t len;
    uint8_t index = history.latest;

    if (NULL == buff) return;

    len = strlen((const char *)buff);
    if (len >= MAX_LINE_LEN) return;  /* command too long */

    /* find the latest one */
    if (0 != index) {
        index--;
    } else {
        index = HISTORY_MAX - 1;
    }

    if (0 != memcmp(history.cmd[index], buff, len)) {
        /* if the new one is different with the latest one, the save */
        memset((void *)history.cmd[history.latest], 0x00, MAX_LINE_LEN);
        memcpy((void *)history.cmd[history.latest], (const void *)buff, len);
        if (history.count < HISTORY_MAX) {
            history.count++;
        }

        history.latest++;
        if (history.latest >= HISTORY_MAX) {
            history.latest = 0;
        }
    }

    history.show = 0;
}


/**
  * @brief              returns a command from the history
  * @param  mode:       TRUE for look up, FALSE for look down
  * @param  p_history:  target history command
  * @retval             TRUE for no history found, FALSE for success
  */
static uint8_t cli_history_show(uint8_t mode, char** p_history)
{
    uint8_t err = true;
    uint8_t num;
    uint8_t index;

    if (0 == history.count) return err;

    if (true == mode) {
        /* look up */
        if (history.show < history.count) {
            history.show++;
        }
    } else {
        /* look down */
        if (1 < history.show) {
            history.show--;
        }
    }

    num = history.show;
    index = history.latest;
    while (num) {
        if (0 != index) {
            index--;
        } else {
            index = HISTORY_MAX - 1;
        }
        num--;
    }

    err = false;
    *p_history = history.cmd[index];

    return err;
}

void cli_init()
{
	shell_queue_init(&cli_rx_buff);
    memset((uint8_t *)&history, 0, sizeof(history));

    for(size_t j = 0; j < MAX_COMMAND_NB; j++){
    	CLI_commands[j].pCmd = "";
    	CLI_commands[j].pFun = NULL;
    }

#ifndef CLI_PASSWORD
    cli_password_ok = true;
    greet();
#endif

    CLI_ADD_CMD("help", cli_help_help, cli_help);
    CLI_ADD_CMD("cls", cli_clear_help, cli_clear);
    CLI_ADD_CMD("log", cli_log_help, cli_log);

    if(CLI_LAST_LOG_CATEGORY > 32){
    	ERR("Too many log categories defined. The max number of log categories that can be user defined is 31.\n");
    }

    LOG(CLI_LOG_SHELL, "Command line successfully initialized.\n");

}

/*
 * Callback function for UART IRQ when it is done receiving a char
 */
void cli_uart_rxcplt_callback(unsigned char rx){
	shell_queue_in(&cli_rx_buff, &rx);
}

/**
  * @brief  handle commands from the terminal
  * @param  commands
  * @retval null
  */
static void cli_rx_handle(shell_queue_s *rx_buff)
{
    static HANDLE_TYPE_S Handle = {.len = 0, .buff = {0}};
    uint8_t i = Handle.len;
    uint8_t cmd_match = false;
    uint8_t exec_req = false;

    /*  ---------------------------------------
        Step1: save chars from the terminal
        ---------------------------------------
     */
    bool newChar = true;
    while(newChar) {
        if(Handle.len < MAX_LINE_LEN) {  /* check the buffer */
        	newChar = shell_queue_out(rx_buff, Handle.buff+Handle.len);

            /* new char coming from the terminal, copy it to Handle.buff */
            if(newChar) {
                /* KEY_BACKSPACE -->get DELETE key from keyboard */
                if (Handle.buff[Handle.len] == KEY_BACKSPACE || Handle.buff[Handle.len] == KEY_DEL) {
                    /* buffer not empty */
                    if (Handle.len > 0) {
                        /* delete a char in terminal */
                        TERMINAL_MOVE_LEFT(1);
                        TERMINAL_CLEAR_END();
                        Handle.buff[Handle.len] = '\0';
                        Handle.len--;
                    }

                } else if(Handle.buff[Handle.len] == KEY_ENTER){
                	exec_req = true;
                	Handle.len++;
                }else if(strstr((const char *)Handle.buff, KEY_DELETE) != NULL){
                	strcpy((char *)&Handle.buff[Handle.len-3], (char *)&Handle.buff[Handle.len+1]);
                	Handle.len -= 3;
            	}else{
                    Handle.len++;
                }

            } else if(cli_password_ok){
                /* all chars copied to Handle.buff */
                uint8_t key = 0;
                uint8_t err = 0xff;
                char *p_hist_cmd = 0;

                if (Handle.len >= 3) {
                    if (strstr((const char *)Handle.buff, KEY_UP) != NULL) {
                        key = 1;
                        TERMINAL_MOVE_LEFT(Handle.len-3);
                        TERMINAL_CLEAR_END();
                        err = cli_history_show(true, &p_hist_cmd);
                    } else if (strstr((const char *)Handle.buff, KEY_DOWN) != NULL) {
                        key = 2;
                        TERMINAL_MOVE_LEFT(Handle.len-3);
                        TERMINAL_CLEAR_END();
                        err = cli_history_show(false, &p_hist_cmd);
                    } else if (strstr((const char *)Handle.buff, KEY_RIGHT) != NULL) {
                        key = 3;
                    } else if (strstr((const char *)Handle.buff, KEY_LEFT) != NULL) {
                        key = 4;
                    }

                    if (key != 0) {
                        if (!err) {
                            memset(&Handle, 0x00, sizeof(Handle));
                            memcpy(Handle.buff, p_hist_cmd, strlen(p_hist_cmd));
                            Handle.len = strlen(p_hist_cmd);
                            Handle.buff[Handle.len] = '\0';
                            xprintf("%s", Handle.buff);  /* display history command */
                        } else if (err && (0 != key)) {
                            /* no history found */
                            TERMINAL_MOVE_LEFT(Handle.len-3);
                            TERMINAL_CLEAR_END();
                            memset(&Handle, 0x00, sizeof(Handle));
                        }
                    }
                }

                if ((key == 0) && (Handle.len > i)) {
                    /* display char in terminal */
                    for (; i < Handle.len; i++) {
                    	xprintf("%c", Handle.buff[i]);

                    }
                }
                break;
            }

        } else {
            /* buffer full */
            break;
        } /*end if(Handle.len > HANDLE_LEN) */
    } /* end While(1) */

    /*  ---------------------------------------
        Step2: handle the commands
        ---------------------------------------
     */
    if(exec_req && !cli_password_ok){
#ifdef CLI_PASSWORD
    	Handle.buff[Handle.len-1] = '\0';
    	if(strcmp((char *)Handle.buff, XSTRING(CLI_PASSWORD)) == 0){
    		cli_password_ok = true;
    		greet();
    	}
    	Handle.len = 0;
#else
    	cli_password_ok = true;
    	greet();
#endif
    }else if(exec_req && (Handle.len == 1)) {
        /* KEY_ENTER -->ENTER key from terminal */
    	PRINT_CLI_NAME();
        Handle.len = 0;
    } else if(exec_req && Handle.len > 1) {  /* check for the length of command */
		NL1();
		Handle.buff[Handle.len - 1] = '\0';
		cli_history_add((char *)Handle.buff);
		char *command = strtok((char *)Handle.buff, " \t");

		/* looking for a match */
		for(i = 0; i < MAX_COMMAND_NB; i++) {
			if(0 == strcmp(command, CLI_commands[i].pCmd)) {
				cmd_match = true;

				//Split arguments string to argc/argv
				uint8_t argc = 1;
				char 	*argv[MAX_ARGC];
				argv[0] = command;

				char *token = strtok(NULL, " \t");
				while(token != NULL){
					if(argc >= MAX_ARGC){
						xprintf(CLI_FONT_RED "Maximum number of arguments is %d. Ignoring the rest of the arguments."CLI_FONT_DEFAULT, MAX_ARGC-1);NL1();
						break;
					}
					argv[argc] = token;
					argc++;
					token = strtok(NULL, " \t");
				}

				if(CLI_commands[i].pFun != NULL) {
					/* call the func. */
					TERMINAL_HIDE_CURSOR();
					uint8_t result = CLI_commands[i].pFun(argc, argv);

					if(result == EXIT_SUCCESS){
						xprintf(CLI_FONT_GREEN "(%s returned %d)" CLI_FONT_DEFAULT, command, result);NL1();
					}else{
						xprintf(CLI_FONT_RED "(%s returned %d)" CLI_FONT_DEFAULT, command, result);NL1();
					}
					TERMINAL_SHOW_CURSOR();
					break;
				} else {
					/* func. is void */
					xprintf(CLI_FONT_RED "Command %s exists but no function is associated to it.", command);NL1();
				}
			}
		}

		if(!cmd_match) {
			/* no matching command */
			xprintf("\r\nCommand \"%s\" unknown, try: help", Handle.buff);NL1();
		}

		Handle.len = 0;
		PRINT_CLI_NAME();

    }


    if(Handle.len >= MAX_LINE_LEN) {
        /* full, so restart the count */
    	xprintf(CLI_FONT_RED "\r\nMax command length is %d.\r\n" CLI_FONT_DEFAULT, MAX_LINE_LEN-1);
    	PRINT_CLI_NAME();
        Handle.len = 0;
    }
}

void cli_run(void)
{
    cli_rx_handle(&cli_rx_buff);
}

void greet(void){
    NL1();
    TERMINAL_BACK_DEFAULT(); /* set terminal background color: black */
    TERMINAL_DISPLAY_CLEAR();
    TERMINAL_RESET_CURSOR();
    TERMINAL_FONT_BLUE();
    xprintf("                             ///////////////////////////////////////////    ");NL1();
    xprintf("                             /////*   .////////////////////////     *///    ");NL1();
    xprintf("            %%%%%%         %%%%%%  ///   ////  //   //////////  //   ////   //    ");NL1();
    xprintf("            %%%%%%        %%%%%%   ///  //////////   ////////  ///  //////////    ");NL1();
    xprintf("           %%%%%%        %%%%%%%%   ((((   (((((((((   ((((((  (((((   .(((((((    ");NL1();
    xprintf("          %%%%%%        %%%%%%%%    (((((((    (((((((  ((((  (((((((((    ((((    ");NL1();
    xprintf("          %%%%%%      %%%%  %%%%    ((((((((((   ((((((  ((  ((((((((((((((  ((    ");NL1();
    xprintf("         %%%%%%%%    %%%%%%   %%%%%%%%  (((*((((((  .(((((((    ((((((( ((((((   ((    ");NL1();
    xprintf("         %%%%*%%%%%%%%%%%%           (((        (((((((((   ((((((((        ((((    ");NL1();
    xprintf("        %%%%   %%%%.             ###################   ##################### (((");NL1();
    xprintf("       %%%%%%          (((      ##################   ##################((((((( ");NL1();
    xprintf("       %%%%               (((( #################   ##############(((((((##    ");NL1();
    xprintf("      %%%%%%                   (((((((((##################((((((((((#######    ");NL1();
    xprintf("     %%%%%%                     ########(((((((((((((((((((################    ");NL1();
    xprintf("     %%%%%%                     ##%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%#%%    ");NL1();
    xprintf("    %%%%%%                      %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    ");NL1();
    xprintf("    %%%%%%                      %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    ");NL1();
    xprintf("                             %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    ");NL1();
    xprintf("                             %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    ");NL1();
    xprintf("µShell for MIK32 Amur - by Paul Kutukov, 2024");NL1();
    xprintf("Original work from https://github.com/ShareCat/STM32CommandLine,");NL1();
    xprintf("As well as form Morgan Diepart (mdiepart@uliege.be)");NL1();
    xprintf("-------------------------------");
    NL2();
    TERMINAL_FONT_DEFAULT();
    PRINT_CLI_NAME();
    TERMINAL_SHOW_CURSOR();
}

/*************************************************************************************
 * Shell builtin functions
 ************************************************************************************/
/**
  * @brief  xprintf the help info.
  * @param  para addr. & length
  * @retval True means OK
  */
uint8_t cli_help(int argc, char *argv[])
{
	if(argc == 1){
	    for(size_t i = 0; i < MAX_COMMAND_NB; i++) {
	    	if(strcmp(CLI_commands[i].pCmd, "") != 0){
		    	xprintf("[%s]", CLI_commands[i].pCmd);NL1();
		        if (CLI_commands[i].pHelp) {
		            xprintf(CLI_commands[i].pHelp);NL2();
		        }
	    	}
	    }
	    return EXIT_SUCCESS;
	}else if(argc == 2){
	    for(size_t i = 0; i < MAX_COMMAND_NB; i++) {
	    	if(strcmp(CLI_commands[i].pCmd, argv[1]) == 0){
		    	xprintf("[%s]", CLI_commands[i].pCmd);NL1();
	    		xprintf(CLI_commands[i].pHelp);NL1();
	    		return EXIT_SUCCESS;
	    	}
	    }
	    xprintf("No help found for command %s.", argv[1]);NL1();
	    return EXIT_FAILURE;
	}else{
		xprintf("Command \"%s\" takes at most 1 argument.", argv[0]);NL1();
		return EXIT_FAILURE;
	}
    return EXIT_FAILURE;
}

/**
  * @brief  clear the screen
  * @param  para addr. & length
  * @retval True means OK
  */
uint8_t cli_clear(int argc, char *argv[])
{
	if(argc != 1){
		xprintf("command \"%s\" does not take any argument.", argv[0]);NL1();
		return EXIT_FAILURE;
	}
    TERMINAL_BACK_DEFAULT(); /* set terminal background color: black */
    TERMINAL_FONT_DEFAULT(); /* set terminal display color: green */

    /* This prints the clear screen and move cursor to top-left corner control
     * characters for VT100 terminals. This means it will not work on
     * non-VT100 compliant terminals, namely Windows' cmd.exe, but should
     * work on anything unix-y. */
    TERMINAL_RESET_CURSOR();
    TERMINAL_DISPLAY_CLEAR();

    return EXIT_SUCCESS;
}

void cli_add_command(const char *command, const char *help, uint8_t (*exec)(int argc, char *argv[])){
	size_t i = 0;
	for(; i < MAX_COMMAND_NB; i++){
		if(strcmp(CLI_commands[i].pCmd, "") == 0){
			CLI_commands[i].pCmd = command;
			CLI_commands[i].pFun = exec;
			CLI_commands[i].pHelp = help;
			break;
		}
	}
	if(i == MAX_COMMAND_NB){
		ERR("Cannot add command %s, max number of commands "
				"reached. The maximum number of command is set to %d.\n" CLI_FONT_DEFAULT,
				command, MAX_COMMAND_NB); NL1();
	}
	LOG(CLI_LOG_SHELL, "Command %s added to shell.\n", command);
}

uint8_t cli_log(int argc, char *argv[]){
	if(argc < 2){
		xprintf("Command %s takes at least one argument. Use \"help %s\" for usage.\n", argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	if(strcmp(argv[1], "on") == 0){
		if(argc < 3){
			xprintf("Command %s on takes at least 3 arguments.\n", argv[0]);
			return EXIT_FAILURE;
		}
		if(strcmp(argv[2], "all") == 0){
			cli_log_stat = 0xFFFFFFFF;
			xprintf("All logs enabled.\n");
			return EXIT_SUCCESS;
		}else{
			for(int i = 2; i < argc; i++){
				cli_enable_log_entry(argv[i]);
			}
			return EXIT_SUCCESS;
		}

	}else if(strcmp(argv[1], "off") == 0){
		xprintf("Turning off all logs\n");
		if(argc < 3){
			xprintf("Command %s on takes at least 3 arguments.\n", argv[0]);
			return EXIT_FAILURE;
		}
		if(strcmp(argv[2], "all") == 0){
			cli_log_stat = 0;
			xprintf("All logs disabled.\n");
			return EXIT_SUCCESS;
		}else{
			for(int i = 2; i < argc; i++){
				cli_disable_log_entry(argv[i]);
			}
			return EXIT_SUCCESS;
		}

	}else if(strcmp(argv[1], "show") == 0){
		for(unsigned int i = 0; i < CLI_LAST_LOG_CATEGORY; i++){
			xprintf("%16s:\t", cli_logs_names[i]);
			if(cli_log_stat&(1<<i)){
				xprintf(CLI_FONT_GREEN"Enabled"CLI_FONT_DEFAULT"\n");
			}else{
				xprintf(CLI_FONT_RED"Disabled"CLI_FONT_DEFAULT"\n");
			}
		}
		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

void cli_disable_log_entry(char *str){
	for(unsigned int i = 0; i < CLI_LAST_LOG_CATEGORY; i++){
		if(strcmp(str, cli_logs_names[i]) == 0){
			xprintf("LOG disabled for category %s.\n", str);
			cli_log_stat &= ~(1<<i);
		}
	}
}

void cli_enable_log_entry(char *str){
	for(unsigned int i = 0; i < CLI_LAST_LOG_CATEGORY; i++){
		if(strcmp(str, cli_logs_names[i]) == 0){
			xprintf("LOG enabled for category %s.\n", str);
			cli_log_stat |= (1<<i);
		}
	}
}
