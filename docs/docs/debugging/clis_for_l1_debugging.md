---
id: clis_for_l1_debugging
title: CLIs for L1 Debugging
description: Debugging L1 issues using various CLIs
keywords:
    - FBOSS
    - OSS
    - L1
    - debug
oncall: fboss_oss
sidebar_position: 2
---
# Introduction

This document enlists different commands that can be used on a FBOSS switch for debugging L1 issues.


# FBOSS2 Commands


## 1.1 fboss2 show interface "interface_name" phy

This command can be used to see the following useful status’ of the ASIC (IPHY = Internal PHY/ASIC/NPU in the below output)



* FEC Statistics
    * Corrected codewords
        * SAI_PORT_STAT_IF_IN_FEC_CORRECTABLE_FRAMES
    * Uncorrected codewords
        * SAI_PORT_STAT_IF_IN_FEC_NOT_CORRECTABLE_FRAMES
    * Pre-FEC BER
        * This is either approximated from the corrected codewords count or accurately represented if the ASIC supports the corrected bits counter (SAI_PORT_STAT_IF_IN_FEC_CORRECTED_BITS)
* PMD Status
    * RX Signal Detect Live and Changed
        * SAI_PORT_ATTR_RX_SIGNAL_DETECT
        * Live represents the status at the last poll time
        * Changed counter increments when either the hardware says the status changed since the last time, or when the live status is different from the previous status that FBOSS read
    * RX CDR Lock Live and Changed
        * SAI_PORT_ATTR_RX_LOCK_STATUS
        * Live represents the status at the last poll time
        * Changed counter increments when either the hardware says the status changed since the last time, or when the live status is different from the previous status that FBOSS read
    * Eye Heights/Widths
        * SAI_PORT_ATTR_EYE_VALUES
    * RX PPM
        * SAI_PORT_ATTR_RX_FREQUENCY_OFFSET_PPM

Note: If any diagnostic is not supported by an ASIC, its status is either reported as N/A or left blank


```
# fboss2 show interface eth7/1/1 phy
 Interface            eth7/1/1
--------------------------------------
 PhyChipType          IPHY
 Link State           UP
 Speed                FOURHUNDREDG
 IPHY Data Collected  0h 0m 4s ago
 IPHY-Line RS FEC
-------------------------------------------------
 IPHY-Line Corrected codewords    13738110125
 IPHY-Line Uncorrected codewords  0
 IPHY-Line Pre-FEC BER            2.5e-12
 IPHY-Line RX PMD  Lane  RX Signal Detect Live  RX Signal Detect Changed  RX CDR Lock Live  RX CDR Lock Changed  Eye Heights  Eye Widths  Rx PPM
-----------------------------------------------------------------------------------------------------------------------------------------------------------
                   0     True                   1                         True              0                                             N/A
                   1     True                   1                         True              0                                             N/A
                   2     True                   1                         True              0                                             N/A
                   3     True                   1                         True              0                                             N/A
                   4     True                   1                         True              0                                             N/A
                   5     True                   1                         True              0                                             N/A
                   6     True                   1                         True              0                                             N/A
                   7     True                   1                         True              0                                             N/A
```



## 1.2 fboss2 show transceiver "interface_name"

This command displays the vendor and DOM information about a transceiver. wedge_qsfp_util command can be used to see a more verbose version of this information.


```
# fboss2 show transceiver eth1/11/1
 Interface  Status  Present  Vendor     Serial      Part Number       FW App Version  FW DSP Version  Temperature (C)  Voltage (V)  Current (mA)             Tx Power (dBm)       Rx Power (dBm)           Rx SNR
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 eth1/11/1  Up      Present  Eoptolink  UPAC160002  EOLO-168HG-02-1T  3.5             209.19          41.55            3.29         86.82,86.62,80.78,75.83  1.57,1.45,1.60,1.30  -0.55,-0.10,-0.58,-0.92  21.64,21.20,20.81,22.82
```





## 1.3 fboss2 show interface "interface_name" prbs "component" capabilities

This command displays the polynomials that each hardware component of an interface supports. A non-empty list of polynomials in front of a component means that other PRBS related FBOSS2 commands can be used to set PRBS and read stats on that component.


```
# fboss2 show interface eth1/11/1 prbs capabilities
 Interface  Component           PRBS Polynomials
--------------------------------------------------------------------------------------------
 eth1/11/1  ASIC
 eth1/11/1  GB_SYSTEM
 eth1/11/1  GB_LINE
 eth1/11/1  TRANSCEIVER_SYSTEM  PRBS31Q,PRBS23Q,PRBS15Q,PRBS13Q,PRBS9Q,PRBS7Q
 eth1/11/1  TRANSCEIVER_LINE    PRBS31Q,PRBS23Q,PRBS15Q,PRBS13Q,PRBS9Q,PRBS7Q,PRBSSSPRQ
```



```
# fboss2 show interface eth1/11/1 prbs transceiver_line capabilities
 Interface  Component           PRBS Polynomials
--------------------------------------------------------------------------------------------
 eth1/11/1  TRANSCEIVER_LINE    PRBS31Q,PRBS23Q,PRBS15Q,PRBS13Q,PRBS9Q,PRBS7Q,PRBSSSPRQ
```




* Use “`fboss2 show interface "interface_name" prbs -h`” to see the available options to use for the “component” argument


## 1.4 fboss2 show interface "interface_name" prbs "component" state

This command displays the state of prbs on a given interface and hardware component


```
# fboss2 show interface eth1/11/1 prbs transceiver_line state
 Interface  Component         Generator  Checker   Polynomial
--------------------------------------------------------------------
 eth1/11/1  TRANSCEIVER_LINE  Disabled   Disabled  0
```




* Use “`fboss2 show interface "interface_name" prbs -h`” to see the available options to use for the “component” argument


## 1.5 fboss2 show interface "interface_name" prbs "component" stats

This command displays PRBS statistics for a given interface and hardware component


```
# fboss2 show interface eth9/2/1 prbs transceiver_line stats
Interface: eth9/2/1
Component: TRANSCEIVER_LINE
Time Since Last Update: 0h 0m 8s
 Lane  Locked  BER        Max BER    SNR      Max SNR  Num Loss Of Lock  Time Since Last Lock  Time Since Last Clear
-------------------------------------------------------------------------------------------------------------------------------
 0     True    1.008e-10  1.012e-10  23.1172  0        0                 0h 0m 12s             0h 0m 12s
 1     True    4.39e-10   4.76e-10   23.1172  0        1                 0h 0m 12s             0h 0m 12s
 2     True    8.53e-11   2.022e-10  23.2969  0        1                 0h 0m 12s             0h 0m 12s
 3     True    8.97e-11   1.089e-10  22.625   0        1                 0h 0m 12s             0h 0m 12s
```



## 1.6 fboss2 set interface "interface_name" prbs "component" state "state"

This command can be used to enable PRBS on an interface


```
# fboss2 set interface eth9/1/1 eth9/2/1 prbs transceiver_line state prbs31 generator checker
PRBS State set successfully
```



```
# fboss2 set interface eth9/1/1 eth9/2/1 prbs transceiver_line state off
PRBS State set successfully
```




* Use “`fboss2 set interface "interface_name" prbs "component" state -h`” to see the available options to use after the ‘state’ argument


## 1.7 fboss2 clear interface "interface_name" prbs "component" stats

This command can be used to clear PRBS stats for an interface


```
# fboss2 clear interface eth9/2/1 prbs transceiver_line stats
Cleared PRBS stats on interfaces eth9/2/1, components TRANSCEIVER_LINE
```



## 1.8 fboss2 set port "interface_name" state disable|enable

This command disables/enables the port on ASIC/NPU


```
# fboss2 set port eth9/1/1 state disable
Disabling port eth9/1/1

# fboss2 set port eth9/1/1 state enable
Enabling port eth9/1/1
```



## 1.9 fboss2 show interface flaps

This command shows the link flap count on all interfaces in various intervals


```
# fboss2 show interface flaps
 Interface Name  1 Min  10 Min  60 Min  Total (since last reboot)
------------------------------------------------------------------------
 eth1/11/1       0      2       12      2335
 eth1/11/5       0      0       0       0
 eth1/12/1       0      0       0       0
```



## 1.10 fboss2 show interface counters fec line|system ber

This command shows pre-FEC BER on all components for a link


```
# fboss2 show interface counters fec line ber 2>1
 Interface Name  ASIC      XPHY_LINE  TRANSCEIVER_LINE
------------------------------------------------------------
 fab1/1/1        9.22e-11  -          0
 fab1/1/2        1.51e-11  -          0

# fboss2 show interface counters fec system ber 2>1
 Interface Name  XPHY_SYSTEM  TRANSCEIVER_SYSTEM
-----------------------------------------------------
 fab1/1/1        -            6.54e-08
 fab1/1/2        -            2.8e-08
```



## 1.11 fboss2 show interface counters fec line|system uncorrectable

This command shows FEC uncorrectable codewords on all components for a link


```

```



# wedge_qsfp_util Commands


## 2.1 wedge_qsfp_util "interface_name" \[--direct-i2c\] \[--verbose\]

This command displays all the transceiver related information


```
# wedge_qsfp_util eth9/1/1
Port 113
  Transceiver Management Interface: CMIS
  Module State: READY
    StateMachine State: TRANSCEIVER_PROGRAMMED
  Module Media Interface: FR4_200G
  Current Media Interface: FR4_200G
  Power Control: HIGH_POWER_OVERRIDE
  FW Version: 7.146
  Firmware fault: 0x0
  EEPROM Checksum: Valid
  Host Lane Signals:       Lane 1       Lane 2       Lane 3       Lane 4
    Tx LOS                 0            0            0            0
    Tx LOL                 0            0            0            0
    Tx Adaptive Eq Fault   0            0            0            0
    Datapath de-init       0            0            0            0
    Lane state             ACTIVATED    ACTIVATED    ACTIVATED    ACTIVATED
  Media Lane Signals:      Lane 1       Lane 2       Lane 3       Lane 4
    Rx LOS                 0            0            0            0
    Rx LOL                 0            0            0            0
    Tx Fault               0            0            0            0
  Host Lane Settings:      Lane 1       Lane 2       Lane 3       Lane 4
    Rx Out Precursor       3            3            3            3
    Rx Out Postcursor      0            0            0            0
    Rx Out Amplitude       3            3            3            3
    Rx Output Disable      0            0            0            0
    Rx Squelch Disable     0            0            0            0
  Media Lane Settings:     Lane 1       Lane 2       Lane 3       Lane 4
    Tx Disable             0            0            0            0
    Tx Squelch Disable     0            0            0            0
    Tx Forced Squelch      0            0            0            0
  Lane Dom Monitors:       Lane 1       Lane 2       Lane 3       Lane 4
    Tx Pwr (mW)            1.06         1.21         1.39         1.17
    Tx Pwr (dBm)           0.25         0.84         1.43         0.70
    Rx Pwr (mW)            1.46         1.65         1.57         1.00
    Rx Pwr (dBm)           1.64         2.18         1.95         0.01
    Tx Bias (mA)           44.00        44.00        44.00        44.00
    Rx SNR                 23.30        23.48        23.30        22.62
  Global DOM Monitors:
    Temperature: 35.44 C
    Supply Voltage: 3.30 V
  Vendor Info:
    Vendor: INNOLIGHT
    Vendor PN: T-FX4FNT-HFS
    Vendor Rev: 3E
    Vendor SN: IWNAIC091886
    Date Code: 230308
  Time collected: Sat Dec 12 10:24:34 2023
```




* Add “--verbose” argument to also display the thresholds configured in module’s eeprom
* Add “--direct-i2c” argument if qsfp_service is not running


## 2.2 wedge_qsfp_util "interface_name" \[--direct-i2c\] --read-reg

This command can be used to read reg(s) on any transceiver


```
# wedge_qsfp_util eth9/1/1 --read-reg --offset 128 --length 128 --page 0x10
qsfp_service is running
Port Id : 113
0080: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0090: 00 11 11 11 11 00 00 00  00 0f 00 00 00 00 00 00
00a0: 0f 0f 33 33 33 33 00 00  00 00 33 33 00 00 00 00
00b0: 00 00 00 00 10 10 10 10  00 00 00 00 0f 00 00 00
00c0: 00 00 00 0f 0f 22 22 00  00 00 00 00 00 33 33 00
00d0: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00e0: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00f0: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
```




* Add “--direct-i2c” argument if qsfp_service is not running
* Multiple interfaces separated by a comma can be specified


## 2.3 wedge_qsfp_util "interface_name" \[--direct-i2c\] --write-reg

This command can be used to write reg(s) on any interface


```
# wedge_qsfp_util eth9/1/1 --write-reg --offset 86 --data 0xF
qsfp_service is running
QSFP 113: successfully write 0x0f to 86.
```




* Add “--direct-i2c” argument if qsfp_service is not running
* Multiple interfaces separated by a comma can be specified


## 2.4 wedge_qsfp_util --capabilities

This command displays which diagnostic capabilities are supported by all the inserted transceivers


```
# wedge_qsfp_util --capabilities
Displaying Diagnostic info for modules
Mod   Diag   VDM   CDB   PRBS_Line PRBS_Sys Lpbk_Line Lpbk_Sys TxDis RxDis SNR_Line SNR_Sys
 0      Y     N     Y        Y         Y        N         Y      Y     Y      Y        N
 1      Y     N     Y        Y         Y        N         Y      Y     Y      Y        N
 6      Y     N     N        N         N        N         N      Y     Y      N        N
 7      Y     N     N        N         N        N         N      Y     Y      N        N
22      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
23      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
34      Y     N     N        N         N        N         N      Y     Y      N        N
35      Y     N     N        N         N        N         N      Y     Y      N        N
40      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
41      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
62      Y     N     Y        Y         Y        N         Y      Y     Y      Y        N
63      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
74      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
75      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
84      Y     N     Y        Y         Y        N         Y      Y     Y      Y        N
85      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
104      Y     Y     Y        Y         Y        Y         Y      Y     Y      Y        N
105      N     N     N        N         N        N         N      N     N      N        N
106      Y     Y     Y        Y         Y        Y         Y      Y     Y      Y        N
107      N     N     N        N         N        N         N      N     N      N        N
112      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
113      Y     N     Y        Y         Y        Y         Y      Y     Y      Y        N
```




* The ‘Mod’ column prints the 0-indexed transceiverID
    * This is the same id printed as “`Port Id`” in the “`wedge_qsfp_util "interface_name"`” command


## 2.5 wedge_qsfp_util "interface_name" --tx-disable|tx-enable \[--channel channelID\]>

This command disables/enables the TX on the line side of the optical transceivers


```
# wedge_qsfp_util eth9/1/1 --tx-disable
Port Id : 113
0000: 1e
I1223 22:01:23.205714 2467802 QsfpUtilTx.cpp:244] Tx Dis/Ena operation on port eth9/1/1 is successful
```




* By default, the command operates on all the lanes/channels applicable for the interface.
* To selectively disable/enable a channel, append the --channel \[channelD\] argument where channelID is 0-indexed \



## 2.6 wedge_qsfp_util "interface_name" --qsfp_reset

This command triggers the hard reset on a transceiver i.e. puts the transceiver in reset and releases the reset


```
# wedge_qsfp_util eth9/1/1 --qsfp-reset
I1223 22:11:12.916870 2501928 wedge_qsfp_util.cpp:2455] Successfully reset ports via qsfp_service with resetType HARD_RESET resetAction RESET_THEN_CLEAR
Successfully reset QSFP modules via qsfp_service
```



## 2.7 wedge_qsfp_util "interface_name" --electrical_loopback

This command enables the loopback mode on the host(system) side of the transceiver


```
# wedge_qsfp_util eth9/1/1 --electrical_loopback
QSFP port eth9/1/1 loopback mode setting done
QSFP 113: done setting module loopback mode electricalLoopback
```



## 2.8 wedge_qsfp_util "interface_name" --optical_loopback

This command enables the loopback mode on the media(line) side of the transceiver. This is a loopback from the fiber towards the fiber.


```
# wedge_qsfp_util eth9/1/1 --optical_loopback
QSFP port eth9/1/1 loopback mode setting done
QSFP 113: done setting module loopback mode opticalLoopback
```



## 2.9 wedge_qsfp_util "interface_name" --host_output_loopback

This command enables the CMIS Host Side Output loopback (page 13h byte 182). Signal received from the fiber is turned back at the host side of the module and sent back out to the fiber. CMIS modules only.


```
# wedge_qsfp_util eth9/1/1 --host_output_loopback
QSFP port eth9/1/1 loopback mode setting done
QSFP 113: done setting module loopback mode hostOutputLoopback
```



## 2.10 wedge_qsfp_util "interface_name" --media_output_loopback

This command enables the CMIS Media Side Output loopback (page 13h byte 180). Signal from the host is turned back at the media side of the module and sent back to the host. CMIS modules only.


```
# wedge_qsfp_util eth9/1/1 --media_output_loopback
QSFP port eth9/1/1 loopback mode setting done
QSFP 113: done setting module loopback mode mediaOutputLoopback
```



## 2.11 wedge_qsfp_util "interface_name" --clear_loopback

This command clears all the loopback mode settings, including all four CMIS loopbacks


```
# wedge_qsfp_util eth9/1/1 --clear_loopback
QSFP port eth9/1/1 loopback mode setting done
QSFP 113: done setting module loopback mode noLoopback
```
