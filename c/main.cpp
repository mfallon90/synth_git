#include "xparameters.h"
#include "xil_printf.h"
#include "xil_exception.h"
#include "xscugic.h"
#include "xil_io.h"
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <cstdio>
#include "constants.hpp"
#include "linked_list.hpp"
#include "functions.hpp"

//FPGA PL BRAM Controller Interrupt ID
#define FPGA_SYNTH_INTR_ID XPAR_FABRIC_FM_SYNTH_WRAPPER_0_INTERRUPT_INTR

//FPGA PL UART Interrupt ID
#define FPGA_UART_INTR_ID XPAR_FABRIC_AXI_UART_WRAPPER_0_MIDI_INTR_INTR

//Interrupt Controller ID
#define GIC_DEVICE_ID XPAR_SCUGIC_0_DEVICE_ID

//Interrupt Handler
#define INTC_HANDLER XScuGic_InterruptHandler

// IRQ Handling Function
void Synth_IRQ_Handler(void *CallbackRef);

// IRQ Handling Function
void UART_IRQ_Handler(void *CallbackRef);

// GIC Configuration
static int GIC_Setup(XScuGic* GicInst, u16 IntrId_1, u16 IntrId_2);

// Instance of the General Interrupt Controller
XScuGic GIC;

linked_list channels;

int main(void)
{

    int Status;
    Status = GIC_Setup(&GIC, FPGA_SYNTH_INTR_ID, FPGA_UART_INTR_ID);

    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Xil_Out32(CTRL_REG_ADDR, TAU_MID);

    while(1){}

return 1;
}

static int GIC_Setup(XScuGic *GicInst, u16 IntrId_1, u16 IntrId_2) {
    int Status;

    XScuGic_Config *IntcConfig;

    //Initialize the interrupt controller.
    IntcConfig = XScuGic_LookupConfig(GIC_DEVICE_ID);
    if (NULL == IntcConfig) {
        return XST_FAILURE;
    }

    Status = XScuGic_CfgInitialize(GicInst, IntcConfig,
    IntcConfig->CpuBaseAddress);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XScuGic_SetPriorityTriggerType(GicInst, IntrId_1, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(GicInst, IntrId_2, 0xA0, 0x3);

    //Connect the interrupt handler to the GIC.
    Status = XScuGic_Connect(GicInst, IntrId_1, (Xil_ExceptionHandler)Synth_IRQ_Handler, 0);
    if (Status != XST_SUCCESS) {
        return Status;
    }

    //Connect the interrupt handler to the GIC.
    Status = XScuGic_Connect(GicInst, IntrId_2, (Xil_ExceptionHandler)UART_IRQ_Handler, 0);
    if (Status != XST_SUCCESS) {
        return Status;
    }

    //Enable the interrupt for this specific device.
    XScuGic_Enable(GicInst, IntrId_1);
    XScuGic_Enable(GicInst, IntrId_2);

    //Initialize the exception table.
    Xil_ExceptionInit();

    //Register the interrupt controller handler with the exception table.
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler) INTC_HANDLER, GicInst);

    //Enable non-critical exceptions.
    Xil_ExceptionEnable();

return XST_SUCCESS;
}

// IRQ Handling function
void Synth_IRQ_Handler(void *CallbackRef) {
    channels.make_available();
}



unsigned char byte_in = 0;
unsigned char state = S_STATUS;

unsigned char test[500];
unsigned char patch_byte;
unsigned char mode;
unsigned char status;
unsigned char on_note;
unsigned char off_note;
unsigned char control_change;
unsigned char velocity;
unsigned char volume;
unsigned char mod_byte = 0;
unsigned char mod_amp_byte;
unsigned int  pitch_bend_lsb;
unsigned int  pitch_bend_msb;
unsigned int  pitch_bend;
unsigned int  i = 0;
unsigned char patch = 0;

// IRQ Handling function
void UART_IRQ_Handler(void *CallbackRef) {

    car_mod notes;

    byte_in = (char) Xil_In32(UART_ADDR);
    test[i] = byte_in;
    i = i+1;

    switch (state) {
        case S_STATUS:
            status = byte_in;
            switch (status) {
                case NOTE_ON        : state = S_NOTE_ON;          break;
                case NOTE_OFF       : state = S_NOTE_OFF;         break;
                case CONTROL_CHANGE : state = S_CONTROL_CHANGE;   break;
                case PITCH_BEND     : state = S_PITCH_BEND_LSB;   break;
                default             : state = S_STATUS;           break;
            }
            break;


        case S_NOTE_ON:
            on_note = byte_in;
            notes = decode_note(on_note, patch, mod_byte);
            if (notes.index != 255) {
                channels.note_on(notes);
            }
            state = S_VELOCITY;
            break;


        case S_NOTE_OFF:
            off_note = byte_in;
            notes = decode_note(off_note, patch, mod_byte);
            if (notes.index != 255) {
                channels.note_off(notes);
            }
            state = S_VELOCITY;
            break;


        case S_CONTROL_CHANGE:
            control_change = byte_in;
            switch (control_change) {
                case PATCH    : state = S_PATCH;    break;
                case MODULATE : state = S_MODULATE; break;
                case MOD_AMP  : state = S_MOD_AMP;  break;
                case VOLUME   : state = S_VOLUME;   break;
                default       : state = S_STATUS;   break;
            }
            break;


        case S_PATCH:
            patch_byte = byte_in;
            decode_patch(patch_byte, &patch);
            if (patch < 6) {
                channels.toggle_modulator(notes, patch);
            }
            state = S_STATUS;
            break;


        case S_VOLUME:
            volume = byte_in;
            decode_volume(volume);
            state = S_STATUS;
            break;


        case S_MODULATE:
            mod_byte = byte_in;
            if (patch == 6) {
                channels.modulate(mod_byte);
            }
            state = S_STATUS;
            break;


        case S_MOD_AMP:
            mod_amp_byte = byte_in;
            decode_mod_amp(mod_amp_byte);
            state = S_STATUS;
            break;


        case S_VELOCITY:
            velocity = byte_in;
            state = S_STATUS;
            break;


        case S_PITCH_BEND_LSB:
            pitch_bend_lsb = (unsigned int) byte_in;
            state = S_PITCH_BEND_MSB;
            break;

        case S_PITCH_BEND_MSB:
            pitch_bend_msb = (unsigned int) byte_in;
            pitch_bend = (pitch_bend_msb << 7) | pitch_bend_lsb;
            channels.bend_pitch(pitch_bend);
            state = S_STATUS;
            break;
    }
}



