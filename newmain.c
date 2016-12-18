/*
 * File:   newmain.c
 * Author: osman
 *
 * Created on October 29, 2016, 2:32 PM
 */
// CONFIG
#pragma config FOSC = INTRCIO   // Oscillator Selection bits (INTOSC oscillator: I/O function on GP4/OSC2/CLKOUT pin, I/O function on GP5/OSC1/CLKIN)
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = OFF      // Power-Up Timer Enable bit (PWRT disabled)
#pragma config MCLRE = ON       // GP3/MCLR pin function select (GP3/MCLR pin function is MCLR)
#pragma config BOREN = ON       // Brown-out Detect Enable bit (BOD enabled)
#pragma config CP = OFF         // Code Protection bit (Program Memory code protection is disabled)
#pragma config CPD = OFF        // Data Code Protection bit (Data memory code protection is disabled)

#include <xc.h>
#include <pic12f675.h>
#include <stdbool.h>

#define _XTAL_FREQ 4000000

typedef enum {
    SunDirectionCellOne = 0, SunDirectionCellTwo = 1, SunDirectionUndefined
} SunDirection;

typedef enum {
    PanelLowLight, PanelNormalLight, PanelDark
} PanelState;

typedef enum {
    LowLight = 6, Dark = 2, NormalLight
} LightCondition;

#define MOTOR_STEP_DURATION 50
#define MOTOR_FULL_ROTATION_DURATION 30 * 1000

#define MOTOR_ON_OF_PIN GP4
#define MOTOR_DIRECTION_PIN GP5
#define LIGHT_PIN GP2

#define SLEEP_DURATION_S (60*5)


void Init();
LightCondition GetLightCondition();
SunDirection FindDirectionOfSun();
void RunPanelLowLightState();
void RunPanelNormalLightState();
void RunPanelDarkState();
void RotatePanelToSunrise();
void RotatePanelToDirection(SunDirection direction);
void ChangeState(PanelState newState);
bool IsLightCondition(LightCondition lightCondition);
void ConfigureForCompareVRef(unsigned char cis,unsigned char vref);
void ConfigureForCompareCells();
void Sleep(unsigned long seconds);
void TurnOffComparator();


PanelState g_PanelState = PanelNormalLight;
int g_TotalDisplacement = 0;


void main() {
    Init();
    GPIObits.LIGHT_PIN = 1;
    while (true) {
        switch (g_PanelState) {
            case PanelNormalLight:
                RunPanelNormalLightState();
                break;
            case PanelLowLight:
                RunPanelLowLightState();
                break;
            case PanelDark:
                RunPanelDarkState();
                break;
        }
        Sleep(SLEEP_DURATION_S);
    }
}

void RunPanelNormalLightState() {
    GPIObits.LIGHT_PIN = 0;
    LightCondition condition = GetLightCondition();
    switch (condition) {
        case Dark:
            ChangeState(PanelDark);
            break;
        case NormalLight:
            RotatePanelToDirection(FindDirectionOfSun());
            break;
        case LowLight:
            ChangeState(PanelLowLight);
            break;
    }
}

void RunPanelLowLightState() {
    GPIObits.LIGHT_PIN = 0;
    LightCondition condition = GetLightCondition();
    switch (condition) {
        case Dark:
            ChangeState(PanelDark);
            break;
        case NormalLight:
            ChangeState(PanelNormalLight);
            break;
    }
}

void RunPanelDarkState() {
    GPIObits.LIGHT_PIN = 1;
    LightCondition condition = GetLightCondition();
    switch (condition) {
        case NormalLight:
            ChangeState(PanelNormalLight);
            break;
        case LowLight:
            //possible sun rise reset panel
            RotatePanelToSunrise();
            ChangeState(PanelLowLight);
            break;
    }
}

void RotatePanelToSunrise(){
    //check displacement direction
    SunDirection sunriseDirection = g_TotalDisplacement < 0 ? SunDirectionCellOne:SunDirectionCellTwo;
    g_TotalDisplacement = 0;
    RotatePanelToDirection(sunriseDirection);
}

void ChangeState(PanelState newState) {
    g_PanelState = newState;
}

void RotatePanelToDirection(SunDirection motorDirection) {
    GPIObits.MOTOR_ON_OF_PIN = 1;
    GPIObits.MOTOR_DIRECTION_PIN = motorDirection;
    int maxMotorDuration = MOTOR_FULL_ROTATION_DURATION;
    while (maxMotorDuration > 0){
        /* Stop when direction of sun changes*/
        SunDirection sunDirection = FindDirectionOfSun();
        if(sunDirection != SunDirectionUndefined && sunDirection != motorDirection){
            break;
        }
        //drive motor
        __delay_ms(MOTOR_STEP_DURATION); 
        maxMotorDuration -= MOTOR_STEP_DURATION;
    }
    GPIObits.MOTOR_ON_OF_PIN = 0;
    
    //calculate displacement
    int displacement = MOTOR_FULL_ROTATION_DURATION - maxMotorDuration;
    if(motorDirection == SunDirectionCellTwo){
        displacement *= -1;
    }
    g_TotalDisplacement += displacement;
}

SunDirection FindDirectionOfSun() {
    if(GetLightCondition() != NormalLight){
        return SunDirectionUndefined;
    }
    ConfigureForCompareCells();
    SunDirection direction = SunDirectionCellTwo;
    if (CMCONbits.COUT) {
        direction =  SunDirectionCellOne;
    }
    TurnOffComparator();
    return direction;
}

LightCondition GetLightCondition() {
    if (IsLightCondition(Dark)) {
        return Dark;
    }
    if (IsLightCondition(LowLight)) {
        return LowLight;
    }
    return NormalLight;
}

bool IsLightCondition(LightCondition lightCondition) {
    ConfigureForCompareVRef(0, lightCondition);
    bool cout = CMCONbits.COUT;
    if (cout) { // VREF > AN
        return false;
    }
    ConfigureForCompareVRef(1, lightCondition);
    cout = CMCONbits.COUT;
    TurnOffComparator();
    return !cout;
}

void Init() {
    ANSEL = 0x03; // Clear Pin selection bits
    TRISIO = 0x0B;
    GPIO = 0;
}

void ConfigureForCompareVRef(unsigned char cis,unsigned char vref) {
    CMCON = 0x06 | (1 << 4) /* VREF > PIN -> COUT = 1 */| (cis << 3); // Multiplexed Input with Internal Reference
    VRCON = 0xA0 | vref;
    __delay_ms(1);
}

void TurnOffComparator(){
    CMCON = 0x00;
    VRCON = 0x00;  
}

void ConfigureForCompareCells() {
    CMCON = 0x02;
    VRCON = 0x00;
    __delay_ms(1);
}

void Sleep(unsigned long seconds){
    for(int i=0;i<seconds;i++){
        __delay_ms(1000);
    }
}
