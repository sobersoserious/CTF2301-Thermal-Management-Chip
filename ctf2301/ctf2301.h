/**
 * @file    ctf2301.h
 * @author  zgs
 * @brief   热管理芯片驱动
 * @version 1.0
 * @date    2026-2-6
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef __CTF2301_REG_H__
#define __CTF2301_REG_H__

#define CTF2301_I2C_7bit_ADDRESS            0x4C

#define CTF2301_POWER_ON_RESET_STATUS       0x33

#define CTF2301_LOCAL_TEMP_MSB              0x00 //Local Temperature:12bit bit[7:0]->temp[11:4]
#define CTF2301_LOCAL_TEMP_LSB              0x15 //Local Temperature:12bit bit[7:4]->temp[3:0]
#define CTF2301_REMOTE_TEMP_MSB             0x01 //Remote Temperature:13bit bit[7:0]->temp[12:5]
#define CTF2301_REMOTE_TEMP_LSB             0x10 //Remote Temperature:13bit bit[7:3]->temp[4:0]

#define CTF2301_CONFIG                      0x03 //STBY TCRITOV
#define CTF2301_CONV                        0x04
#define CTF2301_ONE_SHOT                    0x0F

#define CTF2301_LOCAL_HIGH_SETPOINT_MSB     0x05 //Local High Setpoint:12bit bit[7:0]->temp[11:4]
#define CTF2301_LOCAL_HIGH_SETPOINT_LSB     0x06 //Local High Setpoint:12bit bit[7:4]->temp[3:0]
#define CTF2301_REMOTE_HIGH_SETPOINT_MSB    0x07 //Remote High Setpoint:11bit bit[7:0]->temp[10:3]
#define CTF2301_REMOTE_HIGH_SETPOINT_LSB    0x13 //Remote High Setpoint:11bit bit[7:5]->temp[2:0]
#define CTF2301_REMOTE_LOW_SETPOINT_MSB     0x08 //Remote LOW Setpoint:11bit bit[7:0]->temp[10:3]
#define CTF2301_REMOTE_LOW_SETPOINT_LSB     0x14 //Remote LOW Setpoint:11bit bit[7:5]->temp[2:0]
#define CTF2301_REMOTE_T_CRIT_SETPOINT      0x19
#define CTF2301_REMOTE_T_CRIT_HYS_SETPOINT  0x21

#define CTF2301_TACH_COUNT_MSB              0x47 //bit[7:0]->TAC[13:6]
#define CTF2301_TACH_COUNT_LSB              0x46 //bit[7:2]->TAC[5:0]
#define CTF2301_TACH_LIMIT_MSB              0x49 //bit[7:0]->TACL[13:6]
#define CTF2301_TACH_LIMIT_LSB              0x48 //bit[7:2]->TACL[5:0]

#define CTF2301_ALERT_STATUS                0x02
#define CTF2301_ALERT_MASK                  0x16

#define CTF2301_ENHANCED_CONFIG             0x45
#define CTF2301_PWM_TACH_CONFIG             0x4A
#define CTF2301_PWM_VALUE                   0x4C
#define CTF2301_PWM_FREQUENCY               0x4D

#define CTF2301_SPIN_UP_CONFIG              0x4B

#define CTF2301_LUT1_Temp                   0x50
#define CTF2301_LUT2_Temp                   0x52
#define CTF2301_LUT3_Temp                   0x54
#define CTF2301_LUT4_Temp                   0x56
#define CTF2301_LUT5_Temp                   0x58
#define CTF2301_LUT6_Temp                   0x5A
#define CTF2301_LUT7_Temp                   0x5C
#define CTF2301_LUT8_Temp                   0x5E
#define CTF2301_LUT9_Temp                   0x60
#define CTF2301_LUT10_Temp                  0x62
#define CTF2301_LUT11_Temp                  0x64
#define CTF2301_LUT12_Temp                  0x66

#define CTF2301_LUT1_PWM                    0x51
#define CTF2301_LUT2_PWM                    0x53
#define CTF2301_LUT3_PWM                    0x55
#define CTF2301_LUT4_PWM                    0x57
#define CTF2301_LUT5_PWM                    0x59
#define CTF2301_LUT6_PWM                    0x5B
#define CTF2301_LUT7_PWM                    0x5D
#define CTF2301_LUT8_PWM                    0x5F
#define CTF2301_LUT9_PWM                    0x61
#define CTF2301_LUT10_PWM                   0x63
#define CTF2301_LUT11_PWM                   0x65
#define CTF2301_LUT12_PWM                   0x67

#endif // __CTF2301_REG_H__