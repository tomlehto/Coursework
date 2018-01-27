/*Tomi Lehto and Julius Rintamäki 
Introduction to computer systems 2017 course project
A simple motion-controlled game
for the TI Simplelink SensorTag*/

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>

/* TI-RTOS Header files */
#include <ti/drivers/I2C.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/mw/display/Display.h>
#include <ti/mw/display/DisplayExt.h>
#include <ti/mw/remotecontrol/buzzer.h>


/* Board Header files */
#include "Board.h"

/* jtkj Header files */
#include "wireless/comm_lib.h"
#include "sensors/bmp280.h"
#include "sensors/mpu9250.h"
#include "offlineData.h"


#define CAL_MULTIPLIER 0.8 //multiplier used for calibration
/* Notes for buzzer */
#define C_NOTE 261
#define D_NOTE 294
#define E_NOTE 329
#define F_NOTE 349
#define REST_NOTE 0

/* Task Stacks */
#define STACKSIZE 4096
Char mainTaskStack[STACKSIZE];
Char commTaskStack[STACKSIZE];


/* States */
enum state {MENU, CALIBRATION, GAME};
enum menuState {SELECT_GAME, SELECT_CALIBRATION, SELECT_MODE, SELECT_QUIT};
enum calState {CAL_LEFT, CAL_RIGHT, CAL_UP};
enum playerState {LEFT, RIGHT, DEAD};
enum jumpState {CANJUMP, JUMPED, AIRBORNE};

enum state myState = MENU;
enum menuState myMenuState = SELECT_QUIT;
enum calState myCalState = CAL_LEFT;
enum playerState myPlayerState = DEAD;
enum jumpState playerJump = CANJUMP;

/* Flags and other globals*/
uint8_t buttonWasPressed = 1;
uint8_t powerButtonWasPressed = 0;
uint8_t offlineMode = 0;
uint8_t msgReceived = 0;
uint8_t points;
uint8_t bestPoints = 0;
uint8_t messageCounter = 0;
uint8_t lampOn = 0;
char str[8];
char message;
char payload[16];
uint8_t decodedMsg[8];
uint16_t freqs[] = {C_NOTE, C_NOTE, C_NOTE, REST_NOTE, E_NOTE, REST_NOTE, D_NOTE, D_NOTE, D_NOTE, REST_NOTE, F_NOTE, REST_NOTE, E_NOTE, E_NOTE, REST_NOTE, D_NOTE, D_NOTE, C_NOTE};
float ax, ay, az, gx, gy, gz;

/* Initial MPU limit values */
float leftLimit = -0.5;
float rightLimit = 0.5;
float upLimit = 0.5;

/* Field initialization */
uint8_t field[5][4] = {{0},{0},{0},{0},{0}};

/* Display */
Display_Handle hDisplay;

/* jtkj: Pin Button1 configured as power button */
static PIN_Handle hPowerButton;
static PIN_State sPowerButton;
PIN_Config cPowerButton[] = {
    Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};
PIN_Config cPowerWake[] = {
    Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PINCC26XX_WAKEUP_NEGEDGE,
    PIN_TERMINATE
};


/* Button0 */
static PIN_Handle hButton0;
static PIN_State sButton0;
PIN_Config cButton0[] = {
    Board_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};

/* Reed relay */
static PIN_Handle hRelay;
static PIN_State bRelay;
PIN_Config cRelay[] = {
    Board_RELAY | PIN_INPUT_EN | PIN_PULLDOWN,
    PIN_TERMINATE
};

/* Leds */
static PIN_Handle hLed;
static PIN_State sLed;
PIN_Config cLed[] = {
    Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

static PIN_Handle hBuzzer;
static PIN_State sBuzzer;
PIN_Config cBuzzer[] = {
    Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};


/* MPU config */
static PIN_Handle hMpuPin;
static PIN_State MpuPinState;
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

I2C_Handle i2cMPU;
I2C_Params i2cMPUParams;

static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};


/* Power button handler */
Void powerButtonFxn(PIN_Handle handle, PIN_Id pinId) {
    powerButtonWasPressed = 1;
}

/* Button 0 handler*/
Void Button0Fxn(PIN_Handle handle, PIN_Id pinId){
    buttonWasPressed = 1;
}

/* Converts server data to array*/
void toBinary(uint8_t n){   
    int i;
    for(i = 0;i<7; i++){
       uint8_t a = n << i;
       decodedMsg[i] = a >> 7;
    }
}

/* Updates latest message to field array*/
void updateField(){
	int i, j, k;
	for(i = 0; i<=3; i++){
        for(j = 0; j<=3; j++){
			if (field[i][j] >= 'L' && field[i][j] != 'R' && field[i][j] != ('R'+1)){
				field[i][j] -= 'L';
				if(j>=1){
					field[i][j-1] += 'L';
				}
			}
        }
		for(j = 3; j>=0; j--){
			if (field[i][j] >= 'R'){
				field[i][j] -= 'R';
				if(j<=2){
					field[i][j+1] += 'R';
				}
			}
        }
    }
    for(i = 3; i>=0; i--){
        for(j = 0; j<=3; j++){
            field[i+1][j] = field[i][j]; 
        }
    }
    for(k = 1; k<=2; k++){
        field[0][k] = decodedMsg[k+2];
    }
	if (decodedMsg[1]){
		field[0][0] = 'R';
	}
	else if (decodedMsg[6]){
		field[0][3] = 'L';
	}
}

/* Prints battery voltage on given position */
void printBatteryVoltage(row,  pos){
    uint32_t battery = HWREG(AON_BATMON_BASE + 0x28); //0x40095028 BAT Register
    uint8_t voltageInt;
    float voltageDecimal;
    voltageInt = (battery & 0x700) >> 8;
    voltageDecimal = (battery & 0xff);
    voltageDecimal /= 256;
    float voltage = voltageInt + voltageDecimal;
    sprintf(str, "%.3fV", voltage);
    Display_print0(hDisplay, row, pos, str);

}
/* Checks if player collides with an obstacle*/
uint8_t checkCollision(){
    if ( playerJump != AIRBORNE && ((myPlayerState == LEFT && field[4][1]) || (myPlayerState == RIGHT && field[4][2]) )){
        return 1;
    }
    else{
        return 0;
    }
}
/*Draws screen using our field array*/
void drawScreen(){
    int i;
    if(lampOn){
        for(i = 0; i <= 3; i++){
            Display_print4(hDisplay, 1+2*i, 2, "%s|%s|%s|%s",field[i][0]>='R' ? ">>":field[i][0]>='L' ? "<<": (field[i][0]) ? "##":"  ", 
                                                            field[i][1]>='R' ? ">>":field[i][1]>='L' ? "<<": (field[i][1]) ? "##":"  ",
                                                            field[i][2]>='R' ? ">>":field[i][2]>='L' ? "<<": (field[i][2]) ? "##":"  ",
                                                            field[i][3]>='R' ? ">>":field[i][3]>='L' ? "<<": (field[i][3]) ? "##":"  ");
    													   
            Display_print4(hDisplay, 2+2*i, 2, "%s|%s|%s|%s",field[i][0]>='R' ? ">>":field[i][0]>='L' ? "<<": (field[i][0]) ? "##":"  ", 
                                                            field[i][1]>='R' ? ">>":field[i][1]>='L' ? "<<": (field[i][1]) ? "##":"  ",
                                                            field[i][2]>='R' ? ">>":field[i][2]>='L' ? "<<": (field[i][2]) ? "##":"  ",
                                                            field[i][3]>='R' ? ">>":field[i][3]>='L' ? "<<": (field[i][3]) ? "##":"  ");
        }
    }
    else if(!lampOn){
        for(i = 1; i <= 3; i++){
            Display_print4(hDisplay, 1+2*i, 2, "%s|%s|%s|%s",field[i][0]>='R' ? ">>":field[i][0]>='L' ? "<<": (field[i][0]) ? "##":"  ", 
                                                            field[i][1]>='R' ? ">>":field[i][1]>='L' ? "<<": (field[i][1]) ? "##":"  ",
                                                            field[i][2]>='R' ? ">>":field[i][2]>='L' ? "<<": (field[i][2]) ? "##":"  ",
                                                            field[i][3]>='R' ? ">>":field[i][3]>='L' ? "<<": (field[i][3]) ? "##":"  ");
    													   
            Display_print4(hDisplay, 2+2*i, 2, "%s|%s|%s|%s",field[i][0]>='R' ? ">>":field[i][0]>='L' ? "<<": (field[i][0]) ? "##":"  ", 
                                                            field[i][1]>='R' ? ">>":field[i][1]>='L' ? "<<": (field[i][1]) ? "##":"  ",
                                                            field[i][2]>='R' ? ">>":field[i][2]>='L' ? "<<": (field[i][2]) ? "##":"  ",
                                                            field[i][3]>='R' ? ">>":field[i][3]>='L' ? "<<": (field[i][3]) ? "##":"  ");
        }
    }
}

void menu(){
    if(buttonWasPressed){
        myMenuState++;
        myMenuState %= 4;
        Task_sleep(200000 / Clock_tickPeriod);
        buttonWasPressed = 0;
    }
    printBatteryVoltage(0, 0);
    
    /* Print menu */
    Display_print0(hDisplay, 2, 3, "Main menu");
    Display_print1(hDisplay, 5, 0, "%sStart game", myMenuState == SELECT_GAME ? " >": " ");
    Display_print1(hDisplay, 7, 0, "%sCalibrate", myMenuState == SELECT_CALIBRATION ? " >": " ");
    Display_print2(hDisplay, 9, 0, "%s%sline mode ", myMenuState == SELECT_MODE ? " >":" " , offlineMode ? "Off": "On");
    Display_print1(hDisplay, 11, 0, "%sQuit", myMenuState == SELECT_QUIT ? " >": " ");
    /* Menu FSM */
    switch(myMenuState) {
        case SELECT_GAME:
            if(powerButtonWasPressed){
                Display_clear(hDisplay);
                myState = GAME;
                myPlayerState = LEFT;
                points = 0;
                memset(field,0,20);
                powerButtonWasPressed = 0;
                buzzerOpen(hBuzzer);
            }
            break;
        case SELECT_CALIBRATION:
            if(powerButtonWasPressed){
                Display_clear(hDisplay);
                myState = CALIBRATION;
                powerButtonWasPressed = 0;
            }
            break;
        case SELECT_MODE:
            if(powerButtonWasPressed){
                offlineMode = !offlineMode;
                powerButtonWasPressed = 0;
            }
            break;
        case SELECT_QUIT:
            if(powerButtonWasPressed){
                powerButtonWasPressed = 0;
                Display_clear(hDisplay);
                Display_close(hDisplay);
                Task_sleep(100000 / Clock_tickPeriod);
            	PIN_close(hPowerButton);
                PINCC26XX_setWakeup(cPowerWake);
            	Power_shutdown(NULL,0);
            	break;
            }
            break;
    }
}

void game(){
    if(msgReceived && myPlayerState != DEAD){
        toBinary(message);
        updateField();
        drawScreen();
        msgReceived = 0;
        points++;
        Display_print2(hDisplay, 0,0, "Score:%d HS:%d",points,bestPoints);
        if(playerJump == JUMPED){
            playerJump = AIRBORNE;
        }
        else if(playerJump == AIRBORNE){
            playerJump = CANJUMP;
        }
        Display_print1(hDisplay, 11, 2, "%s", &payload[1]);
        buzzerSetFrequency(freqs[points%18]);
    }
    if(buttonWasPressed && myPlayerState != DEAD){
        lampOn = !lampOn;
        PIN_setOutputValue(hLed, Board_LED0, lampOn);
        Display_clearLines(hDisplay, 1, 2);
        drawScreen();
        buttonWasPressed = 0;
    }
    if(PIN_getInputValue(Board_RELAY)){
        Display_print0(hDisplay, 11, 1,"STRONG GRAVITY");
    }
    /* Player FSM */
    switch(myPlayerState){
        case LEFT:
            Display_print2(hDisplay, 9, 4, "|%s|%s|", playerJump ? "()":"()", field[4][2] ? "##":"  ");
            Display_print2(hDisplay, 10, 4, "|%s|%s|", playerJump ? "<>":"/\\", field[4][2] ? "##":"  ");
            if(ax>rightLimit){
                myPlayerState = RIGHT;
                Display_print2(hDisplay, 9, 4, "|%s|%s|", field[4][1] ? "##":"  ", playerJump ? "()":"()");
                Display_print2(hDisplay, 10, 4, "|%s|%s|",  field[4][1] ? "##":"  ", playerJump ? "<>":"/\\");
            }
            else if(!PIN_getInputValue(Board_RELAY) && ay>upLimit){
                if(playerJump == CANJUMP){
                    playerJump = JUMPED;
                }
            }
            if(checkCollision()){
                myPlayerState = DEAD;
                Display_clear(hDisplay);
            }
            break;
        case RIGHT:
            Display_print2(hDisplay, 9, 4, "|%s|%s|", field[4][1] ? "##":"  ", playerJump ? "()":"()");
            Display_print2(hDisplay, 10, 4, "|%s|%s|",  field[4][1] ? "##":"  ", playerJump ? "<>":"/\\");
            if(ax<leftLimit){
                myPlayerState = LEFT;
                Display_print2(hDisplay, 9, 4, "|%s|%s|", playerJump ? "()":"()", field[4][2] ? "##":"  ");
                Display_print2(hDisplay, 10, 4, "|%s|%s|", playerJump ? "<>":"/\\", field[4][2] ? "##":"  ");
            }
            else if(!PIN_getInputValue(Board_RELAY) && ay>upLimit){
                if(playerJump == CANJUMP){
                    playerJump = JUMPED;
                }
            }
           if(checkCollision()){
                myPlayerState = DEAD;
                Display_clear(hDisplay);
           }
            break;
        case DEAD:
            Display_print0(hDisplay, 3, 3, "YOU DIED");
            Display_print0(hDisplay, 4, 3, "PRESS UP");
            Display_print0(hDisplay, 5, 3, "TO LEAVE");
            buzzerSetFrequency(100);
            if(points>bestPoints){
                bestPoints = points;
            }
            Display_print1(hDisplay, 7, 3, "Score: %d", points);
            Display_print1(hDisplay, 8, 3, "Best:  %d", bestPoints);
            if(buttonWasPressed){
                PIN_setOutputValue(hLed, Board_LED0, 0);
                myState = MENU;
                Display_clear(hDisplay);
                buttonWasPressed = 0;
                buzzerClose();
            }
            break;
    }
}

void calibration(){
    Display_print0(hDisplay, 3, 1, "SELECT WITH PWR");
    switch(myCalState){
        case CAL_LEFT:
            Display_print0(hDisplay, 5, 1, "CALIBRATE LEFT");
            if(powerButtonWasPressed){
                leftLimit = ax*CAL_MULTIPLIER;
                myCalState = CAL_RIGHT;
                powerButtonWasPressed = 0;
                Task_sleep(100000 / Clock_tickPeriod);
                Display_clear(hDisplay);
            }
            break;
        case CAL_RIGHT:
            Display_print0(hDisplay, 5, 1, "CALIBRATE RIGHT");
            if(powerButtonWasPressed){
                rightLimit = ax*CAL_MULTIPLIER;
                myCalState = CAL_UP;
                powerButtonWasPressed = 0;
                Task_sleep(100000 / Clock_tickPeriod);
                Display_clear(hDisplay);
            }
            break;
        case CAL_UP:
            Display_print0(hDisplay, 5, 1, "CALIBRATE UP");
            if(powerButtonWasPressed){
                upLimit = ay*CAL_MULTIPLIER;
                myCalState = CAL_LEFT;
                powerButtonWasPressed = 0;
                myState = MENU;
                Task_sleep(100000 / Clock_tickPeriod);
                Display_clear(hDisplay);
            }
            break;
    }
}

/* Communication Task */
Void commTask(UArg arg0, UArg arg1) {
    uint16_t senderAddr;
	int32_t result = StartReceive6LoWPAN();
	if(result != true) {
		System_abort("Wireless receive mode failed");
	}
    while (1) {
        if(!offlineMode){ // only send/receive messages while online
            if (myState == GAME && powerButtonWasPressed){ //send points to server with powerbutton while in game
                buzzerSetFrequency(1000);
                memset(str,0,8);
                sprintf(str, "%d JEE", points);
                Send6LoWPAN(IEEE80154_SERVER_ADDR, str, strlen(str));
                StartReceive6LoWPAN();
                powerButtonWasPressed = 0;
            }
            if (GetRXFlag() == true) {
                memset(payload,0,16);
                Receive6LoWPAN(&senderAddr, payload, 16);
                message = payload[0];
                msgReceived = 1;
            }
        }
        else if(offlineMode){
            int i;
            for(i=1000000;i>0;i--){}//delay
            message = offlineMessages[points%69];
            msgReceived = 1;
            
        }    
    }
}

/* Main task */
Void mainTask(UArg arg0, UArg arg1) {
    /* I2C init for MPU */    
    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2CMPU\n");
    }
    /* Setup MPU */
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);//mpu power on
    Task_sleep(100000 / Clock_tickPeriod);
	mpu9250_setup(&i2cMPU);

    /* Init Display */
    Display_Params displayParams;
	displayParams.lineClearMode = DISPLAY_CLEAR_BOTH;
    Display_Params_init(&displayParams);

    hDisplay = Display_open(Display_Type_LCD, &displayParams);
    if (hDisplay == NULL){
        System_abort("Error initializing Display\n");
    }

    /* Main loop */
    while (1){
        mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz); //get gyro data
    	Task_sleep(10000 / Clock_tickPeriod);
    	/* Main FSM */
        switch(myState){
            case MENU:
                menu();
                break;
            case GAME:
                game();    
                break;
            case CALIBRATION:
                calibration();
                break;
        }
    }
}

Int main(void) {
    /*Task variables */
	Task_Handle hMainTask;
	Task_Params mainTaskParams;
	Task_Handle hCommTask;
	Task_Params commTaskParams;

    /* Init board */
    Board_initGeneral();
    Board_initI2C();

	/* Init buttons */
	hPowerButton = PIN_open(&sPowerButton, cPowerButton);
	if(!hPowerButton) {
		System_abort("Error initializing power button shut pins\n");
	}
	if (PIN_registerIntCb(hPowerButton, &powerButtonFxn) != 0) {
		System_abort("Error registering power button callback function");
	}
    hButton0= PIN_open(&sButton0, cButton0);
    if (!hButton0) {
        System_abort("Error initializing button pins\n");
    }
    if (PIN_registerIntCb(hButton0, &Button0Fxn) != 0) {
		System_abort("Error registering power button callback function");
	}
	
    /* Init reed relay */
    hRelay = PIN_open(&bRelay, cRelay);
	if(!hRelay) {
		System_abort("Error initializing relay pin\n");
	}
    
    /* Init leds and buzzer*/
    hLed = PIN_open(&sLed, cLed);
    if(!hLed) {
        System_abort("Error initializing LED pin\n");
    }
    hBuzzer = PIN_open(&sBuzzer, cBuzzer);
    if(!hBuzzer) {
        System_abort("Error initializing Buzzer pin\n");
    }
    
    /* Init main task */
    Task_Params_init(&mainTaskParams);
    mainTaskParams.stackSize = STACKSIZE;
    mainTaskParams.stack = &mainTaskStack;
    mainTaskParams.priority=2;
    hMainTask = Task_create(mainTask, &mainTaskParams, NULL);
    if (hMainTask == NULL) {
    	System_abort("Task create failed!");
    }
    
    /* Init Communication Task */
    Task_Params_init(&commTaskParams);
    commTaskParams.stackSize = STACKSIZE;
    commTaskParams.stack = &commTaskStack;
    commTaskParams.priority=1;
    Init6LoWPAN();
    hCommTask = Task_create(commTask, &commTaskParams, NULL);
    if (hCommTask == NULL) {
    	System_abort("Task create failed!");
    }
    
    /* Start BIOS */
    BIOS_start();

    return (0);
}

