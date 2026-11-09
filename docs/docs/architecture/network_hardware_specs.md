# Meta Network Hardware Switch Requirements v1.0

This document captures the unified hardware requirements for network switches to be seamlessly integrated into Meta's data center infrastructure.

Note: These requirements are complementary to the [Meta Switch Architecture](https://meta.github.io/fboss/docs/architecture/meta_switch_architecture) (a.k.a. BMC Lite Requirements), which covers internal switch hardware and software design. This RFC focuses on how the switch integrates into Meta's data center infrastructure.


#### **Versioning:**

* This document will have source-control-based versioning and tracking for all changes.
* This document has a major revision number, which will be updated for major changes.
* This document has a minor revision number, which will be updated for any change in specification which will necessitate a hardware or software change in the switch.


## **1. Power**

### **1.1 Power Delivery Architecture**

Meta data centers use the ORv3 (Open Rack V3) rack and power system. In this architecture, rack-mounted PSU shelves take 3-phase AC input from tap boxes via whip cables and convert it to 48V DC, which is distributed to IT gear through a vertical busbar. BBU (Battery Backup Unit) shelves provide backup power during AC outages — they do not actively load-share during normal operation. No PDU is required for ORv3; whip cables connect directly to the PSU shelf.

Network switches receive power in two ways depending on form factor:

- Pizza-box switches (e.g., Rack Switches) are PSU-type agnostic. They can use either an AC PSU (with AC power cords, for standalone/EIA-19" deployment) or a DC/DC PSU (powered from the 48V busbar in ORv3/ORv3N racks). AC and DC PSUs are not mixed in the same deployment.

- Blade switches (e.g., Scale Up Blades) have no PSU, and they are powered by the 48V rack busbar.

- The ORv3 ecosystem includes several rack variants (see table below).

| Rack Variant | Power (Max/Roofline) | PSU/BBU Configuration | Busbar & Physical Specs | Primary Use/Notes |
| :---- | :---- | :---- | :---- | :---- |
| ORv3 Standard | ~15KW (N+1) | 1 PSU shelf, 6x 3KW modules, 3KW BBU | Whip config: 2x 30A per shelf. | Used for pizza-box switches and compute servers. Whip config: NORAM 2x 20A, EMEA 1x 32A. |
| ORv3-600A | ~29KW (strict busbar limit) | 2 PSU shelves, 6x 3KW modules, 3KW BBU | Network-dedicated racks for DCTypeX MDF and DCTypeF server rows. | Whip config: 2x 30A per shelf. |
| ORv3N | ~28KW | Varies PSU shelves, 6x 3KW modules, 3KW BBU | Used for AI/HPC: Includes PMM for shelf-level monitoring. | Network-dedicated racks for DCTypeX MDF and DCTypeF server rows. |
| ORv3-HPR v1 | ~92KW | 3 PSU shelves (33KW each), 6x 5.5KW modules, 5.5KW BBU | Next-gen AI racks. | Used for AI/HPC: Includes PMM for shelf-level monitoring. |
| HPR v2 | ~190KW | Upgraded shelves, 12KW PSU modules, 10-12KW BBU | Currently in EVT. | Next-gen AI racks. |
| HPR v3 | up to 300KW | LVDC Power Rack (side rack) |  | Currently in EVT. |

Requirements:

- R-PWR-011: Pizza-box switches shall be compatible with ORv3 Standard (15KW, 375A) and ORv3N/ORv3-600A (28-29KW, 600A) rack power systems, or with AC input for EIA-19” racks. Pizza-box switches shall be PSU-type agnostic, supporting both AC PSU (for standalone/EIA-19" deployment) and DC/DC PSU (for ORv3 busbar deployment).
- R-PWR-012: Liquid-cooled blade switches shall be compatible with ORv3-HPR rack power systems (92KW roofline, 700A busbar clip, 6x 5.5KW PSU modules per shelf, 5+1 redundancy).
- R-PWR-014: The switch power input connector shall mate with the rack busbar without tools and shall support hot-insertion/removal without impacting adjacent equipment.
- R-PWR-020: DC/DC power input to IT gear shall operate within: Steady state: 45–53 VDC; Transient (< 2ms): 41–57 VDC; Input dynamics test: 45V for 5ms, ramp to 53V in 2.5ms, 53V for 5ms, ramp down to 45V in 2.5ms.
- R-PWR-030: Rack busbar voltage ripple shall not exceed 500mV peak-to-peak.
- R-PWR-040: Rack busbar voltage transient response shall be within +/-3%.
- R-PWR-050: 48V power input shall follow Meta DC PCB reliability design requirements with sufficient clearance to other signals and circuits.
- R-PWR-060: 48V power input shall be immune to voltage transitions between the rack BBU and rack power shelf during AC outage events.

### **1.2 Power Conversion Efficiency**

Internal switch power conversion can follow a 48V to 12V to component voltage architecture.

- R-PWR-100: 48V-to-12V conversion shall achieve minimum 97% efficiency (includes VR conversion loss and PCB distribution loss).
- R-PWR-110: 12V-to-ASIC conversion should achieve minimum 88% average efficiency across all ASIC power rails.
- R-PWR-120: 12V-to-optics conversion shall achieve minimum 94% efficiency on the 3.3V power input to pluggable optics connectors.
- R-PWR-130: Main power distribution loss from power busbar to power bricks should be under 1% of system total power consumption.

### **1.3 Power Monitoring**

- R-PWR-200: System shall monitor full system, fan tray, switch ASIC, supervisor module, and optics power consumption in real time with better than +/-5% accuracy.
- R-PWR-210: Any PCB fault that shorts between power and ground shall trigger immediate power circuit shutdown to prevent thermal meltdown.
- R-PWR-220: Each input overcurrent protection device shall be monitored and readable by software.

### **1.4 Power Budget**

- R-PWR-250: System full-loadtypical power consumption shall be defined under the following standard test conditions:
  - Full line-speed Layer-3 packet forwarding on all user ports/channels with random payload pattern in a fixed packet size of 600B (front-end fabric switch) or 3,200B (back-end network switch) at 100% line rate bandwidth.
  - Under typical PVT conditions: 30°C ambient at 6,000 ft altitude, nominal voltages, and average components (not FF or SS corners).
  - Any exception to the defined power budget requires Meta's approval.

### **1.5 PSU Requirements (Pizza Box Form Factor)**

For standalone pizza-box switches with integrated PSUs:

- R-PWR-300: PSU shall provide TTL-compatible presence signal readable by CPU.
- R-PWR-310: PSU shall provide always-on standby 12V/2.5A rail with +/-60mV regulation and droop control for current sharing.
- R-PWR-320: PSU shall provide 12V main output controllable via TTL signal, with +/-30mV regulation.
- R-PWR-330: Universal AC input: 90–305VAC, 47–63Hz. Full load at high line (208V+).
- R-PWR-340: PSU efficiency shall exceed 80PLUS Titanium at 230VAC/25C.
- R-PWR-350: PSUs shall provide equal current sharing in 1+1 redundancy configuration.
- R-PWR-360: AC/DC output ripple shall target less than 5%.
- R-PWR-370: PSU shall provide PMBus interface for status and control. BMC shall control PSON_L signal through PLD devices.
- R-PWR-380: PSU delayed start: main output waits 50ms+ after PSON_L assertion; turns off in less than 5ms after de-assertion.
- R-PWR-390: PSU shall contain ID EEPROM accessible via I2C, containing model number, ratings, airflow direction.
- R-PWR-400: PSU shall provide isolation: 1500Vrms+ input-to-chassis, 3000Vrms+ input-to-output.
- R-PWR-410: PSU shall implement automatic shutdown for internal/external faults and catastrophic temperatures.
- R-PWR-420: PSU sound pressure 50dBA or less at 30C/6000ft under 60% load.
- R-PWR-430: PSU input impedance shall meet Meta "Power Supply Impedance Specification" .
- R-PWR-440: AC input fuse shall be installed at both live and neutral line traces.
- R-PWR-450: PSU MCU/DSP shall be field-programmable via I2C interface.

## **2. Racking and Mechanical**

### **2.1 Rack Compatibility**

- R-RACK-010: Switch chassis/blade shall be compatible with Meta ORv3 rack frame directly or with an ORv3 rack canister.
- R-RACK-015: Switch chassis may be required to be compatible with EIA-19” rack frame.
- R-RACK-020: Switch chassis/blade shall be capable of operating on ORv3-HPR racks (92KW+ rating, 600A).
- R-RACK-030: The chassis shall support 4-post flush mounting with front panel flush with the two front rack posts.
- R-RACK-040: Rack elevation shall follow Meta-approved block diagrams.

### **2.2 Form Factor Dimensions**

| Form Factor | Dimensions (H x W x D) | Notes |
| :---- | :---- | :---- |
| 2OU blade | 94.5mm x 529mm max x 805mm | Must fit ORv3-HPR canister |
| 1OU blade | 46.5mm x 529mm max x 805mm | Must fit ORv3-HPR canister |
| Pizza box  (EIA-19") | Platform-specific (1–4RU) | Standard 19 inch rack-mount, for ORv3N and standalone racks |
|  Pizza box (ORv3 21") |  Platform-specific (1–4RU), ~529mm width | Must fit ORv3 standard rack, 48V busbar power, no PSU needed |
| Chassis or Canister | Platform-specific | Space for RMC, -RSW/RTSW, PSU, BBU, fiber patch |

### **2.3 Canister (Rack-Level Blade Enclosure or Chassis)**

- R-RACK-100: Canister bottom slot shall include a drip pan. Top & bottom shall have cross-brace support. Middle cross-brace support shall be considered depending on canister size.
- R-RACK-120: Canister shall allow rack power busbar to deliver 48V power to blades.
- R-RACK-130: Canister shall have liquid manifold and blind-mate connectors (PBMC) to blades. Rack supply and return liquid manifold shall interface with the external liquid supply and return through OCP Large Quick Connector.
- R-RACK-140: Switch blades in the canister shall be independent for liquid cooling and power input.
- R-RACK-150: Switch blades in the canister shall be hot-swappable and tool-less.
- R-RACK-170: Cable cartridge in canister shall be field replaceable and preferably tool-less.

### **2.4 Mechanical Design**

- R-RACK-200: Chassis shall be designed with smooth and rounded finish without sharp edges.
- R-RACK-210: Touch temperature of user-contactable surfaces shall not exceed IEC 60950-2005 Table 4C limits.
- R-RACK-220: All rack-level cable/fiber routing shall be within ORv3 rack cable manager channel.
- R-RACK-240: All non-touch-point plastic parts should be black. All touch-point should be green (Pantone-375C)
- R-RACK-250: Fiber cable routing shall allow easy and harmless replacement of individual optic modules without affecting adjacent ports.
- R-RACK-260: Asset tag shall be placed on the front surface of sub-chassis and legible with fibers fully cabled.
- R-RACK-270: Shall meet 18-000160 Meta Cosmetic and Inspection Guidelines

### **2.5 Grounding**

- R-RACK-300: Chassis will be grounded through the busbar.
- R-RACK-310: PCB mounting holes shall short chassis ground and signal ground together.
- R-RACK-320: Front port metal cages (RJ45, USB, OSFP) shall connect directly with signal ground.
- R-RACK-330: Chassis front and rear shall include ESD banana jacks for operator grounding.

## **3. Cooling**

### **3.1 Cooling Boundary Conditions**

- R-COOL-010: All air-cooled components shall stay within thermal specs and operate indefinitely under: 35C ambient at sea level; 30C ambient at 6000ft elevation; 30C ambient at 6000ft with one fan rotor failure.
- R-COOL-011: The liquid-cooled platforms with hybrid cooling or : 30C air / 30C liquid (PG25) inlet when it is deployed with facility liquid cooling (FLC); 35C air / 40C liquid (PG25) inlet  when it is deployed with air assisted liquid cooling (AALC);

### **3.2 Airflow Requirements**

- R-COOL-020: Airflow-to-power ratio: CFM less than 0.146 x Total Power (W) at sea level; CFM less than 0.18 x Total Power (W) at 6000ft.
- R-COOL-030: Air flow pressure drop shall be minimized to keep fan speed low.
- R-COOL-040: Chassis shall support front-to-back airflow direction.
- R-COOL-050: The chassis air flow should be able to overcome 0.1 inch water of back pressure .
- R-COOL-060: PSU airflow direction shall be consistent with the rest of the chassis.
- R-COOL-070: PSU shall have dedicated intake airflow channel or air baffle.

### **3.3 Fan Requirements**

- R-COOL-100: Fan tray presence shall be readable by CPU through FPGA.
- R-COOL-110: Each fan shall report tachometer readings and accept independent PWM signals.
- R-COOL-120: Fan PWM step in control CPLD shall be less than or equal to 2.5%.
- R-COOL-130: Fan PWM control shall help regulate fan inrush current.
- R-COOL-140: Individual fan failures shall be determinable and indicated to CPU.
- R-COOL-150: Fan tray shall be replaceable without disrupting adjacent fan tray airflow.
- R-COOL-160: Fan tray shall be field-replaceable, hot-swappable, with foolproof mechanism to prevent incorrect installation.
- R-COOL-170: Fan tray shall have visual identification of airflow direction (labeling or mechanical differentiation).
- R-COOL-180: With one fan tray removed, the system should operate indefinitely at 30C/6000ft. If not achievable, service window duration shall require Meta approval.
- R-COOL-190: Fan service shall implement N+1 redundancy. If hot-swap is not possible, N+2 fan rotor redundancy is required.
- R-COOL-200: There shall be a second-source fan with identical performance characteristics.
- R-COOL-210: Worst fan location shall be identified for cooling impact analysis.

### **3.4 Fan Speed Control (FSC)**

- R-COOL-300: System shall implement FSC algorithm for overall thermal management based on temperature sensor readings and fan status.
- R-COOL-310: With all fans healthy, thermal design shall deliver exhaust delta-T of 12.3C or more at ambient 30C or less and altitude 6000ft or less.
- R-COOL-320: 3C margin on optics case temperatures is strongly recommended while maintaining the delta-T target.
- R-COOL-330: Multiple fan failures exceeding thermal limits shall trigger logged automatic shutdown per agreed thresholds.

### **3.5 Acoustics**

- R-COOL-400: System sound pressure level shall not exceed 80dBA, at 1 meter distance from the front of the system,  at 30C ambient and 6000ft altitude, per ISO7779 or ANSI_ASA_S12.10.

### **3.6 Liquid Cooling Architecture**

- R-LCOOL-010: If the hardware is determined to be partially or fully liquid cooled, switch ASIC and optical modules shall be liquid cooled with cold plates.
- R-LCOOL-020: Components other than switch ASIC and optical module can be either air cooled or liquid cooled depending on the system architecture.

### **3.7 Coolant and Flow**

- R-LCOOL-100: Coolant: Propylene glycol 25% (PG25) / DowFrost LC-25.
- R-LCOOL-110: Liquid flow to liquid-cooled components (LPM) shall be 1.5x or less of power of liquid-cooled components (kW).
- R-LCOOL-120: Liquid cooling supply can provide 25 PSI at 1.5 LPM/kW at rack level.
- R-LCOOL-130: Liquid flow rate allocation shall aim for equivalent liquid-side temperature difference across all trays within one rack.
- R-LCOOL-140: Both inlet conditions shall be supported: 40C liquid inlet with air assisted liquid cooling (AALC) or liquid-to-air CDU; 30C liquid inlet with facility liquid cooling.

### **3.8 Quick Disconnects for Liquid Cooling**

- R-LCOOL-200: Tray-level: Blind Mate Quick Connects/Disconnects (BMQC, per OCP Liquid Cooling Quick Disconnect Specification). Or, Pivoting Blind Mate Coupling (PBMC).
- R-LCOOL-210: Rack-level: Manual mate OCP Large Quick Connector for rack-level supply/return manifold.
- R-LCOOL-220: Blade liquid pipes connect at the rear, blind-mated to rack manifold.

### **3.9 Leak Detection and Protection**

- R-LCOOL-300: Liquid leak detection circuit shall be designed to prevent hazardous leaking accidents.
- R-LCOOL-310: System shall verify the integrity of the leak detection circuit.
- R-LCOOL-320: Leak incidents shall be reported from system to RMC via RJ45 interface on front panel. System power on/off shall be controlled through the same RJ45 interface by RMC.
- R-LCOOL-330: System shall have a leak injection mechanism to emulate leak events for testing.
- R-LCOOL-340: Leak detection circuit shall be functional even if x86 CPU or BMC is not alive.
- R-LCOOL-350: Hardware protection mechanism shall shut down power to switch ASIC in case of leaking.
- R-LCOOL-360: Leak detection sensors (wire or spot type) shall communicate with BMC/CPLD to raise alarm to RMC. Detection at component, tray, and rack level with adjustable sensitivity/granularity.
- R-LCOOL-370: Mechanical design shall contain liquid within localized spaces in the tray or drip pan.
- R-LCOOL-380: System shall shut down power delivery and turn off coolant supply upon major, tray-level leak detection.

### **3.10 Temperature Monitoring**

- R-TEMP-010: Multiple thermal sensors shall be optimally placed at critical locations: inlet, exhaust, optics, switch ASIC, retimer, micro-server CPU, DIMM, etc.
- R-TEMP-020: All temperature sensors shall have accuracy +/-2C or better. Recommended +/-1C for FSC-critical sensors.
- R-TEMP-030: CPU shall monitor all critical component temperatures in real time.
- R-TEMP-040: CPU shall accurately monitor airflow consumption. It can be calculated based on fan RPM and PWM values.
- R-TEMP-050: Onboard temperatures exceeding operating range shall be logged and trigger automatic shutdown per agreed thresholds.

## **4. Interfaces and Front Panel**

### **4.1 Management Ports**

- R-INTF-010: System shall have one RJ45 console port on front panel, 9600bps baud rate, connected directly to BMC UART.
- R-INTF-020: System shall have one RJ45 management Ethernet (OOB) port on front panel, supporting 1000BASE-T, connected to OOB switch internally.
- R-INTF-030: OOB RJ45 port shall have one Link LED and one Activity LED.
- R-INTF-040: System shall have one USB 2.0 port on front panel (CPU as host), using a USB Type-A connector. USB 3.0 port on a Type-A connector is also acceptable.
- R-INTF-050: For liquid-cooled platforms, one RJ45 is required on the front panel for leak detection communication with RMC.

RMCv2 supports 2 types of leakage sensing cable (LSC) interface: Legacy interface and CAN Bus interface. The interface specifications are covered by the RMC v2 Specification (Meta-internal document; contact your Meta partner for access), and here is a snapshot.

*Legacy LSC Interface*

| Pin | Signal Name | Description | I/O | Tray side | RMC side |
| :---- | :---- | :---- | :---- | :---- | :---- |
| 1 | Tray_SCL | I2C interface to leakage detection logic. C | I | Pulled up to 3.3V_STBY with 4.7Kohm |  |
| 2 | GND |  |  | Connect to GND | Connect to GND |
| 3 | Tray_SDA | I2C interface to leakage detection logic | I/O | Pulled up to 3.3V_STBY with 4.7Kohm |  |
| 4 | Small_Leak_L | Small leak signal. Once a small leak is detected, then tray logic will pull this signal low. | O | Connect to CPLD with isolator | Pulled up to 3.3V_STBY through 4.7Kohm at RMC |
| 5 | CPLD_Ready_L | CPLD ready signal. Active Low. Tray will hold HighZ until CPLD is fully up. | O | Connect to CPLD with isolator | Pulled up to 3.3V_STBY through 4.7Kohm at RMC  |
| 6 | Tray_PowerEnable_L  | Active Low. Tray will pull this pin Low. RMC will connect this pin to CPLD. Once this pin is pulled high by RMC, the tray shall turn off main power and leave STBY power on.  | I | Pulled down through 4.7Kohm | RMC will pull signal high to 3.3V_STBY if a leak that required trays shutdown occurs. |
| 7 | Tray_Present_L | Tray Present signal. Active low. | O | Connect to GND through  0ohm resistor | Pulled up to 3.3V_STBY through 4.7Kohm  |
| 8 | Tray_Leak_L | Tray leak signal. Once a tray leak is detected at the base of the tray, then tray logic will pull this signal low. | O | Connect to CPLD with isolator, pulled down through 100Kohm | Pulled up to 3.3V_STBY through 1Kohm at RMC |

*CAN Bus LSC Interface*

| Pin | Signal Name | Description |
| :---- | :---- | :---- |
| 1 | CAN_H | CAN_H bus line (dominant high) |
| 2 | CAN_L | CAN_L bus line (dominant low) |
| 3 | CAN_GND | Ground |
| 4 | N/C |  |
| 5 | N/C |  |
| 6 | N/C |  |
| 7 | GND | Ground |
| 8 | N/C |  |

- R-INTF-100: Front panel management ports shall be ordered: OOB RJ45 eth0 on top; Console RJ45 and Leak Detection RJ45 below. Other layouts require Meta approval.

### **4.2 Switch ASIC Management Ports**

- R-INTF-200: System shall have one QSFP28 ASIC management Ethernet port.
- R-INTF-210: QSFP28 shall support 100G-CR4 and 50G-CR2 breakout mode.
- R-INTF-220: QSFP28 host channel insertion loss (ASIC BGA ball to QSFP connector footprint) shall be less than 6.8dB at 12.89GHz for 100G-CR4 compliance.
- R-INTF-230: QSFP28 shall support up to 3.0m/26AWG DAC and 2.0m/30AWG DAC.
- R-INTF-240: QSFP28 shall have one tri-color LED managed by software for link status.

### **4.3 Panel LED Requirements**

- R-LED-010: All panel LEDs shall be visible with fiber cables and power cords installed.
- R-LED-020: Panel LEDs shall not bleed into neighboring LEDs. In-chassis debug LEDs shall not bleed through panel.
- R-LED-030: Panel LED component material reliability shall be reviewed. Reliability test plan/report required. LED reliability monitored since EVT.
- R-LED-040: Port LED intensity (R, G, B) shall be programmable with 256+ levels. OSFP port LEDs controlled by CPU software, not switch ASIC. Hardware shall support blinking without software toggling.
- R-LED-050: LED brightness and labeling shall meet OCP Panel Indicator Specification.

### **4.4 LED Color Definitions**

| Req # | Category | Pri | LED Name | Definition | Behavior |
| :---- | :---- | :---- | :---- | :---- | :---- |
| R1.10.20 | RJ45 LED | R0 | Link LED | Single color Green LED located on the left side of the port. Driven by OOB switch or PHY. | - Link up: Green - Link down: Off |
|  |  |  | Activity LED | Single color Amber LED located on the right side of the port. Driven by OOB switch or PHY. | - TX or RX activity: Blinking Amber - No activity: Off |
| R1.10.30 | System LED | R0 | SYS LED | Tri-color LED for system status. Default OFF. Driven by CPU. | - Blue: All FRUs are present. - Amber: One or more FRUs are not present |
|  |  |  | FAN LED (no need for 100% liquid cooled system) | Tri-color LED for fan status. Default OFF. Driven by CPU. | - Blue: All fans are present - Amber: One or more fans are not present. |
|  |  |  | LEAK LED | Tri-color LED for leak status. Default OFF. Driven by CPLD. | - Red: leakage detected and main power is turned off. - Amber: Small leakage detected and main power is on. |
|  |  |  | PWR LED (if system design is based on-board DC/DC modules) | Tri-color LED for Power status. Default blue ON. Driven by CPLD. | - Blue: Power rails work fine. - Amber: One or more than one of the power rails failed. |
|  |  |  | PSU LED (if system has on-board PSUs) | Tri-color LED for PSU status. Default OFF. Driven by CPLD. | - Blue: Every PSU expected for the system's configured input power type (AC/DC) is present and operational. - Amber: An expected PSU is absent, or any installed PSU reports a fault. |
|  |  |  | SMB LED | Tri-color LED for SMB status. Default OFF. Driven by CPU. | - Blue: No out-of-range voltage or temperature sensor - Amber: One or more sensors are out of range. |
| R1.10.40 | FRU LED | R0 | SCM FRU LED | Tri-color LED on SCM panel. Default Amber. Driven by CPU. | - Blue: No out-of-range voltage or temperature sensor. COMe is not in power-off or suspended states. - Amber: One or more sensors are out of range, or COMe is in suspended state. - Amber flashing: Attention from service technician required. |
| R1.10.60 | FRU LED | R0 | Fantray FRU LED | Bi-color LED located on fantray. Default OFF. Driven by CPU. | - Blue: Fan RPM within the normal range. |
| R1.10.70 | FRU LED | R0 | PSU FRU LED | Single Bi-color LED on PSU. Default OFF. Driven by PSU microcontroller. | - Both Off: Status Off. |
| R1.10.80 | User Port LED | R0 | User Port LED | Tri-color LED for each logical user channel. Default OFF. Driven by CPU. | For the port LED, the intensity of each color (R, G, or B) shall be programmable with a granularity of at least 256 levels. |
| R1.10.90 | FRU LED | R0 | DC-PEM/PSU FRU LED | Single Bi-color LED on DC PEM/PSU. Default OFF. Driven by PSU microcontroller. | - Both Off: Status Off. |

### **4.5 Port Numbering**

- R-LED-100: Port numbering: lowest port = 1, increasing top-to-bottom then left-to-right.
- R-LED-110: Fan numbering: lowest = 1, increasing top-to-bottom then left-to-right viewed from front.

## **5. Environmental and Reliability**

### **5.1 Operating Conditions**

- R-ENV-010: Operating temperature: 0C to 35C. The system shall cold start at both extremes.
- R-ENV-020: Storage temperature: -40C to 70C.
- R-ENV-030: Operating humidity: 5% to 90% relative humidity.
- R-ENV-040: Manufacture burn-in: 45C with elevated fan PWM.
- R-ENV-050: High/low temperature thermal shock per GR-63-CORE (NEBS Requirements: Physical Protection).

### **5.2 Vibration and Shock**

-
- R-ENV-100: System shall meet ***Meta Mechanical Test Requirements for NW products*** (latest rev to be shared upon request) at both L10 and L11 level.

### **5.3 Reliability**

- R-REL-010: System shall be designed for 24x7 continuous operation with active service life greater than 7 years.
- R-REL-020: MTBF at 45C of the individual FRU cards (i.e. SCM, SMB, etc) shall individually be longer than 300,000 hours.
- R-REL-030: MTBF calculation for the above should follow Telcordia SR-332 Issue 4 methodology at both 25C and 45C.
- R-REL-040: Reliability Demonstration Testing (RDT) should aim to demonstrate a minimum 5 years of useful life (through temperature acceleration factor) prior to mass production
- R-REL-050: All FRU shall support minimum 250 insertion/removal cycles, or maximum cycle number defined for the mating parts specification, whichever is higher.
- R-REL-060: Ongoing Reliability Testing (ORT) to follow Meta’s specified test and sampling plan in order to validate batch-to-batch variation in mass production.
-

## **6. Redundancy and Serviceability**

### **6.1 Field Replaceable Units (FRUs)**

- R-FRU-010: All FRUs shall allow tool-less installation and extraction. Finger pressure shall not exceed 7 lbs; hand pressure shall not exceed 10 lbs.
- R-FRU-020: All FRU insertion/removal shall be directly detectable by x86 CPU.
- R-FRU-030: FRU interface design shall minimize potential mechanical damage during insertion/removal.
- R-FRU-040: All FRUs shall have keying feature to prevent wrong-orientation and/or offset insertion.
- R-FRU-050: A defective FRU shall not damage the electronic circuit in the system.
- R-FRU-060: The card shall have ground pin first, power pin 2nd, signal pin 3rd in mate-first/break-last sequence.

### **6.2 Hot-Swappable Components**

| Component | Hot-Swap Required | Notes |
| :---- | :---- | :---- |
| OSFP/QSFP transceivers | Yes | Inrush 0.1A/us or less, max 0.6A per OSFP MSA |
| E1.S SSD | Yes | Sanitized / destroyed after removal per Meta security requirements.  |
| Fan tray | Yes (blind-mate preferred) | — |
| RunBMC module | Tool-less replaceable | Not hot-swap |
| DIMM | Tool-less replaceable | Not hot-swap |
| PSU (pizza box) | Yes | — |
| Blade (rack) | Yes | On energized rack |

### **6.3 Serviceability**

- R-FRU-100: DIMM, SSD, and RunBMC replacement shall not require mechanical tools or opening mechanical parts.
- R-FRU-110: Sub-chassis (such as the Switch Main Board module) shall meet OSHA weight requirements for single-person carry and include carrying handle(s).
- R-FRU-120: Sub-chassis shall have safety stop feature before or at center of gravity; technician must release before removal.
- R-FRU-130: Sub-chassis shall have guide pin(s) to guide mating into chassis (minimum one).

## **7. BMC / System Controller**

### **7.1 BMC Architecture**

- Refer to [Meta Switch Architecture](https://meta.github.io/fboss/docs/architecture/meta_switch_architecture/)

### **7.2 Rack Management Controller (RMC)**

- Refer to the RMC v2 Specification (Meta-internal document; contact your Meta partner for access).

## **8. CPU / Microserver**

- Refer to [Meta Switch Architecture](https://meta.github.io/fboss/docs/architecture/meta_switch_architecture/) for detailed specification
- R-CPU-040: CPU heat sink shall be optimized to minimize airflow while maintaining adequate margin under TDP conditions.
- R-CPU-050: Front panel SCM/RPU shall have OAR (Open Air Ratio) optimized to prevent bypass relative to SMB airflow.
- R-CPU-210: SCM/COMe shall support PCIe link to FPGA on SMB. Additional PCIe links should be implemented if connector pin count allows.

## **9. Switching ASIC and Data Plane**

### **9.1 Switch ASIC Platform Requirements**

- R-ASIC-020: The switch ASIC shall operate at optimized SVS or AVS voltage and optimized clock frequency for most efficient power consumption.
- R-ASIC-030: Hardware protection mechanism shall shut down ASIC power when junction temperature exceeds normal operating threshold, independent of SDK software or ASIC driver.

### **9.2 User Ports (Data Ports)**

| SerDes Generation | Port Config (example) | Optics Speeds |
| :---- | :---- | :---- |
| 212G-PAM4 | 32x or 64x OSFP-RHS | 1.6TE, 2x800GE, 4x400GE, 8x200GE, 2x400GE |
| 112G-PAM4 | 32x or 64x OSFP-IHS | 800GE, 2x400GE, 2x200GE |

- R-ASIC-100: Each port shall be individually configurable regardless of other ports modes.
- R-ASIC-110: The SerDes channel between the switch ASIC and the front panel data ports shall meet the relevant requirements of OIF and IEEE.
- R-ASIC-120: Port mapping between ASIC and front panel shall allow ASIC memory bandwidth evenly distributed on horizontal, vertical, and diagonal ports. Port mapping requires Meta approval.

### **9.3 Per-Port LEDs**

- R-ASIC-200: Each OSFP user port should have two tri-color LEDs for link status (one per logical channel). LEDs visible with fiber cable installed. LED placement approved by Meta. No bleeding into neighbors.

### **9.3 Clocking**

- R-ASIC-300: System shall support IEEE1588v2 PTP end-to-end 1-step transparent clock meeting T-TC Class A requirement in ITU-T G.8273.3. Class A specifies max +/-50 ns constant time error (max|cTE|) per hop for transparent clocks under normal operating conditions.
- R-ASIC-310: System shall use a TCXO as the frequency source of the switch ASIC Ethernet SerDes reference clocks, and it should have an initial frequency tolerance of +/-1PPM.

## **10. Firmware (FPGA / CPLD)**

### **10.1 Device Management**

- R-FW-010: FPGA and CPLD device selection shall be approved by Meta.
- R-FW-020: Meta will provide major FPGA RTL design for final MP. Vendor/ODM responsible for FPGA bring-up, troubleshooting, and regression verification.
- R-FW-030: Vendor shall implement CPLD or FPGA with embedded internal Flash through GitHub version control, shared with Meta.
- R-FW-040: All programmable and field-upgradeable devices shall have version readable by CPU through IOB FPGA. BMC shall read IOB FPGA revision directly.
- R-FW-050: FPGA/CPLD design shall be backward compatible throughout NPI phase.
- R-FW-060: FPGA/CPLD remote programming script shall check compatibility between image header and target device before committing Flash programming.

### **10.2 FPGA Functions**

- R-FW-100: FPGA shall support OSFP MSA CMIS 4.0 and 5.0 for optics management. I2C access at each OSFP port shall be independent. I2C transactions shall be atomic.
- R-FW-110: If SRAM configuration technology is used, SRAM static error detection shall report dynamic configuration errors to software.
- R-FW-120: Meta will provide fan PWM control and RPM sensing RTL building blocks and CSR. Vendor shall integrate into fan control PLD under Meta supervision.
- R-FW-130: CSR files shall be implemented per Meta format to define CPLD registers. CSR files version-controlled through GitHub.

### **10.3 Verification**

- R-FW-200: Vendor shall prepare FPGA designs for initial EVT bring-up to verify circuit design.
- R-FW-210: Vendor shall deliver a checklist to verify Meta FPGA functions upon completion of FPGA bring-up.
- R-FW-220: FPGA verification test plan shall be Meta-approved. Test reports updated at EVT/DVT/PVT exit. Weekly bug count tracked until DVT exit.
- R-FW-230: FPGA access/verification utility source code delivered to Meta no later than EVT exit.

## **11. Security**

Note: Detailed security requirements (secure boot, attestation, anti-rollback, etc.) are defined in the separate FBOSS Platform Security Requirements document (Meta-internal document; contact your Meta partner for access).

- R-SEC-010: Meta Authentication Module connector shall be mounted on SCM, interconnected with micro-server complex.
- R-SEC-020: Vendor shall follow security requirements defined in "FBOSS Platform Security Requirement”.
- R-SEC-030: As outlined in Section 6.2, SSDs upon removable will be sanitized and/or destroyed following Meta’s ERAD security policy.

## **12. Optics / Transceivers**

### **12.1 Supported Optics (212G-PAM4 Platforms)**

| Transceiver | TDP | Priority |
| :---- | :---- | :---- |
| 2x800G FR4 OSFP-RHS | 28W | R0 |
| 2x800G DR4 OSFP-RHS | 32W | R0 |
| 2x800G LRO OSFP-RHS | TBD | R0 |
| 1.6T ZR OSFP-RHS | 40W | R0 |
| 1.6T ZR+ OSFP-RHS | 45W | R1 |
| 1.6T eloop OSFP-RHS | — | R0 (mfg test) |
| 1.6T AEC OSFP-RHS | — | R0 (eng test) |

### **12.2 Supported Optics (112G-PAM4 Platforms)**

| Transceiver | Priority |
| :---- | :---- |
| 2x400G FR4-Lite OSFP-IHS | R0 |
| 2x400G FR4 OSFP-IHS | R0 |
| 2x400G LR4 OSFP-IHS | R0 |
| 2x400G DR4 OSFP-IHS | R0 |
| 800G ZR OSFP-IHS | R1 |
| 800G passive eloop OSFP-IHS | R0 (mfg test) |
| 800G AEC OSFP-IHS | R0 (eng test) |

### **12.3 Optics Mechanical & Thermal**

- R-OPT-010: All ports must have dust caps installed for shipment.
- R-OPT-020: Ducting should minimize airflow consumption while meeting optic module temperature requirements.
- R-OPT-030: Distribution of perforations in front panel shall be optimized to minimize temperature difference between optical modules.

### **12.4 Optics Qualification Test Cases**

- R-OPT-100: Each optics transceiver type shall be qualified with:
1. Module detection and setup
2. DOM (Digital Optical Monitoring) readout
3. Fiber plug/un-plug test
4. Module plug/un-plug test (20+ cycles for DAC)
5. Port enable/disable test (20+ cycles for DAC)
6. L2/L3 snake test using data traffic with FEC

## **13. EEPROM / FRU Identification**

- R-EEPROM-010: All FRUs (except fan trays) shall contain an ID EEPROM accessible via I2C serial bus. EEPROM format shall align with [Meta EEPROM v6 definition](https://meta.github.io/fboss/docs/platform/meta_eeprom_format_v6).
- R-EEPROM-020: PSU EEPROM shall contain model number, input power rating, output power rating, airflow direction. Accessible even if AC power is lost.

## **14. Software Requirements**

### **14.1 Architecture Compliance**

- R-SW-010: Vendor shall design HW and SW according to [Meta Network Switch System Requirements](https://meta.github.io/fboss/docs/architecture/meta_switch_architecture) (Meta Switch Architecture / BMC Lite).
- R-SW-020: Vendor shall follow [OpenBMC software requirements](https://meta.github.io/fboss/docs/openbmc/openbmc_software_requirements) and PR guidelines.
- R-SW-030: Vendor shall develop/test BSP codes meeting [BSP development requirements](https://meta.github.io/fboss/docs/platform/bsp_development_requirements) and [API specifications](https://meta.github.io/fboss/docs/platform/bsp_api_specification).
- R-SW-040: Vendor shall develop platform service config files per [PlatformManager](https://meta.github.io/fboss/docs/platform/platform_manager), [platform SW requirements](https://meta.github.io/fboss/docs/platform/platform_sw_requirements), and [services support requirements](https://meta.github.io/fboss/docs/platform/services_support_requirements).
- R-SW-050: System design shall implement [PlatformManager](https://meta.github.io/fboss/docs/platform/platform_manager) for flexible BSP driver management.

### **14.2 FBOSS Integration**

- R-SW-100: Vendor shall contribute to FBOSS SW codebase with required logic and tests.
- R-SW-110: Vendor shall develop and test QSFP and LED service changes for the platform.
- R-SW-120: If the platform includes CPO, the vendor shall follow CPO requirements.
- R-SW-130: Vendor shall run FBOSS hardware tests, agent hardware tests, and benchmarks ensuring 100% pass rate for all SDK-supported features.
- R-SW-140: Warm boot upgrade / ISSU shall not exceed BGP GR timer.

### **14.3 Development Process**

- R-SW-200: Use CentOS Stream 9 and Linux kernel 6.16.y for diagnostic OS. Upgradeable as specified by Meta.
- R-SW-210: Bring up switch ASIC with SDK. Upgrade to GA release. Data plane testing must include L2 and L3.
- R-SW-220: Vendor shall implement source code review and approval system with Meta access.
- R-SW-230: Vendor shall respond to Meta engineer code diff feedback within 2 working days.
- R-SW-240: COMe BIOS shall send platform name through dmidecode in Meta-specified field.

## **15. NPI Process Requirements**

### **15.1 EVT (Engineering Verification Test)**

- R-NPI-010: EVT test plans provided 2+ weeks before hardware bring-up.
- R-NPI-020: Complete EVT bring-up with OpenBMC, CentOS, and ASIC SDK before shipping to Meta.
- R-NPI-030: Deliver CPLD design; validate CPLD and FPGA with major functions and all external interfaces.
- R-NPI-040: Functional tests shall cover data path (switch ASIC, user ports), control path (CPU, DDR, SSD, PCIe, FPGA, MIIM), and management path (BMC, CPLD, fans, PSU, I2C/SPI buses).
- R-NPI-050: Complete L2 and L3 snake tests on all ports.
- R-NPI-060: Complete power cycle tests with full functional tests each cycle.
- R-NPI-070: Complete firmware upgrade tests.
- R-NPI-080: Complete EDVT (SI of signals, VRs, FRU hotswap).
- R-NPI-090: Complete SerDes tuning with recommended settings.
- R-NPI-100: Complete 4-corner testing.
- R-NPI-110: Complete EMC and safety compliance pre-tests.
- R-NPI-120: Set up long-term mini-cycle test (30 UUT for blades/switches, 1 rack for rack-level).
- R-NPI-130: Initial round of DFx (Assembly, Manufacturability, and Test) reviewed.

### **15.2 DVT (Design Verification Test)**

- R-NPI-200: Fix all EVT issues requiring board spin. Implement new feature requests.
- R-NPI-210: Regression tests covering all switch functions.
- R-NPI-220: Complete packaging qualifications for system and FRU.
- R-NPI-230: Complete EDVT and MDVT for any design changes.
- R-NPI-240: Complete formal EMC and safety compliance tests.
- R-NPI-250: Complete pre-RDT (sample count and duration approved by Meta).
- R-NPI-260: Long-term mini-cycle test (30 UUT or 2 racks).
- R-NPI-270: All “hot” DFx items closed or mitigations in place.

### **15.3 PVT (Production Verification Test)**

- R-NPI-300: Fix all DVT issues. Implement new feature requests.
- R-NPI-310: Finish mechanical hard tooling and qualification.
- R-NPI-320: Acquire EMC and safety compliance certificates.
- R-NPI-330: Continue long-term mini-cycle test (3 racks for rack-level).
- R-NPI-340: Complete formal RDT (as outlined in Section 5.3 R-REL-040)

### **15.4 Rack-Level NPI (Liquid-Cooled Platforms)**

- R-NPI-400: Complete RMC 10GE interconnection verification.
- R-NPI-410: Complete PSU and BBU function test (MODBUS, AC-loss, load sharing, capacity).
- R-NPI-420: Complete rack-level SerDes tuning.
- R-NPI-430: Complete liquid cooling tuning and parameter profile.
- R-NPI-440: Complete leak detection and reporting function validation.
- R-NPI-450: Complete rack-level robustness test with error injection: leak, PSU/BBU failure, AALC/facility LC failure, RMC failure, switch blade leak.

## **16. Documentation Deliverables**

### **16.1 Design Documents**

- R-DOC-010: Hardware design specification (block diagrams, power calculations, SerDes mapping, I2C/SPI mapping, CPLD/FPGA registers)
- R-DOC-020: CPLD/FPGA design specification
- R-DOC-030: OpenBMC design specification and user guide (RunBook)
- R-DOC-040: Diagnostics design specification and user guide (RunBook)
- R-DOC-050: Software upgrade procedure
- R-DOC-060: Hardware/software release notes (BKC, change list, known issues)
- R-DOC-070: Hardware troubleshooting guide
- R-DOC-080: System runbook (updated biweekly during EVT/DVT/PVT)
- R-DOC-090: Meta Mechanical Deliverables Matrix for all NPI design phases

### **16.2 Test Reports**

All the test reports are expected for both EVT and DVT phases, with some required for PVT phases.

- R-DOC-100: System functional test plan and report
- R-DOC-105: FPGA/CPLD functional test plans and reports
- R-DOC-110: EDVT test plan and report (high-speed SI, low-speed signal SI, power supplies, hotswap inrush)
- R-DOC-115: TDR / S-parameter test plan and report (short/medium/long channels)
- R-DOC-120: Ethernet interface IEEE test report
- R-DOC-125: Optical transceiver integration test plan and report
- R-DOC-130: PSU integration test plan and report (including power sharing)
- R-DOC-135: 4-corner test plan and report
- R-DOC-140: Thermal test plan and report
- R-DOC-145: Acoustic test plan and report
- R-DOC-150: Mechanical test plan and report (MDVT)
- R-DOC-155: Environmental and reliability test plans and reports

### **16.3 Compliance and Quality**

- R-DOC-200: EMC compliance plans, reports, and certificates
- R-DOC-205: Safety compliance plans, reports, and certificates
- R-DOC-210: Material environmental compliance reports
- R-DOC-215: MTBF report
- R-DOC-220: Second source report and qualification
- R-DOC-225: Statement of volatility
- R-DOC-230: Component derating report
- R-DOC-235: Packaging test plan and report
- R-DOC-240: FAI (First Article Inspection) reports
- R-DOC-245: DFx reports

### **16.4 Design Files**

- R-DOC-300: Schematics (PDF and native format)
- R-DOC-305: Rack layout and cable routing schematics
- R-DOC-310: ECAD/MCAD PCBA Allegro, IDF, and DXF files
- R-DOC-315: 3D CAD files (STEP and native Creo format)
- R-DOC-320: Flotherm PDML files for thermal simulations
- R-DOC-325: Full FPGA, CPLD, OpenBMC, Diag, and SDK source code and images

## **17. Regulatory and Compliance**

- R-REG-010: EMC compliance testing shall be executed at system level (or canister level for rack-mounted blades).
- R-REG-020: Safety compliance testing shall be executed at system level (or canister level).
- R-REG-030: All platforms shall meet Meta Infrastructure Data Center Hardware Compliance Requirements (the SIE Hardware Design Guide covers environmental, seismic, acoustic, and safety standards for DC-deployed equipment).
- R-REG-040: Material environmental compliance per Meta Materials of Concern Standard. This includes RoHS Directive (2011/65/EU), REACH Regulation, and California Proposition 65.
- R-REG-050: PSU shall satisfy safety, EMC, and environmental specifications defined by Meta compliance team.
- R-REG-060: AC power cords must meet UL 62, VW1 Flammability rating. Heat shock test required.

## **18. Manufacturing, Diagnostics & Quality**

### **18.1 Manufacturing Diagnostics**

* R-MFG-010: Vendor shall develop diagnostics software for manufacturing and hardware validation, with a documented runbook/user guide by production.
* R-MFG-020: Diagnostics shall generate CPU self-traffic requiring no external equipment for manufacturing testing.
* R-MFG-030: Diagnostics shall self-start upon power cycle or from command line after configuration.
* R-MFG-040: Tests shall be integrated into a single script with a consolidated pass/fail result representing the entire test list.
* R-MFG-050: Diagnostic suite shall cover all major circuit blocks including external loopback paths.
* R-MFG-060: Test duration shall support both time-based and cycle-count control.
* R-MFG-070: Test records shall be logged to non-volatile memory and recoverable for later analysis.
* R-MFG-080: Test results shall be predetermined pass/fail with timestamp and temperature — no operator interpretation required.
* R-MFG-090: Diagnostics shall use high-level command syntax (not raw register reads/writes) and provide individual I2C commands for fault isolation.
* R-MFG-100: Diagnostics shall run independently on each FRU in addition to the full system.
* R-MFG-110: Online firmware updates (BIOS, CPLD, FPGA) shall be available on the manufacturing line.
* R-MFG-120: Diagnostics shall provide loopback tests at multiple checkpoints across the end-to-end data path for troubleshooting.
* R-MFG-130: Optical test stations with loopback or snake test capability shall verify alignment, contamination, and port mapping.

### **18.2 In-Fleet Diagnostics**

* R-MFG-200: ODM shall support Meta to enable in-fleet diagnostics without interrupting traffic.
* R-MFG-210: ODM shall support in-fleet full system diagnostics when the system is drained (not carrying traffic). The test should not send out any test traffic to other switches in the fleet.
* R-MFG-220: ODM shall provide a hardware sanity check tool meeting Meta's published requirements.

### **18.3 Manufacturing Quality Processes**

#### **Factory Test Requirements**

* R-MFG-300: ODM shall follow Meta’s specified manufacturing test flow which may include stations such as ICT, Board Functional Test (BFT), System Test, Burn-In, and Final Test. The exact test cases for each of these stations will be determined by ODM but aligned with Meta.
* R-MFG-310: All test records should be uploaded and stored within ODM’s shopfloor system for a minimum 2 years. Test history should be easily traceable and accessible as needed.
* R-MFG-320: Daily yield reporting and failure Paretos required during NPI builds.
* R-MFG-330: DFx analysis required on PCB/PCBA prior to gerber-out and post-build prior to next phase. Reports shared with Meta.
* R-MFG-340: First article inspection report required prior to first PVT/pilot shipments (test summary, EEPROM dump, test reports).

#### **Clean Room & Environmental Controls**

* R-MFG-350: For optical products, certain handling and assembly steps should be conducted within either a Class 10K Clean Room [ISO 7] or Class 1K Clean Room [ISO 6].
* R-MFG-360: 24/7 live monitoring for particles/temp/humidity in clean room areas (RH 45–70%, Temp 24–27°C for optics areas).

#### **ESD Management**

* R-MFG-370: ESD management per ANSI/ESD S20.20, including annual training and system audit.
* R-MFG-380: Operator-level ESD controls required: wrist strap, static control garment, ESD footwear, seating, and gloves per applicable ANSI/ESD standards.

### **18.5 Part Management & Traceability**

* R-MFG-390: ODM shall align with Meta on part numbering scheme from system level down to PCBA.
* R-MFG-400: Device and packaging labeling shall follow Meta guidelines for serialized vs. non-serialized assets.
* R-MFG-410: Critical commodities require date/lot code and serial numbers stored within the ODM’s SAP system for traceability.
* R-MFG-420: PCN (Part Change Notification) process for mass production shall align with Meta's guidelines (to be provided).
* R-MFG-430: ODM shall support Meta end-to-end supply chain security review.
* R-MFG-440: ODM shall provide assembly SOPs and troubleshooting guides.

### **18.6 RMA & Failure Analysis**

* R-MFG-450: Every RMA shall be tracked with a minimum: date of request, RMA #, location, failure reason, and high-level finding at ODM.
* R-MFG-460: RMAs requiring further failure analysis shall include an 8D report. 80% of open FAs must be closed within 30 days.

## **Appendix A: Glossary**

| Term | Definition |
| :---- | :---- |
| AALC | Air Assisted Liquid Cooling |
| AVL | Approved Vendor List |
| BBU | Battery Backup Unit |
| BMQC | Blind Mate Quick Connect |
| BSP | Board Support Package |
| CDU | Coolant Distribution Unit |
| COMe | Computer on Module (Express) |
| CPO | Co-Packaged Optics |
| CSR | Control and Status Registers |
| DSFv2 | Distributed Scheduled Fabric v2 |
| EDVT | Electrical Design Verification Test |
| ETSW | Edge Top-of-rack Switch |
| EVT | Engineering Verification Test |
| FBOSS | Facebook Open Switching System |
| FRU | Field Replaceable Unit |
| FSC | Fan Speed Control |
| FTSW | Fabric Top-of-rack Switch |
| IOB | I/O Board |
| JXSW | Junction Switch (front-end network) |
| MDVT | Mechanical Design Verification Test |
| NSF | Non-Scheduled Fabric |
| NPI | New Product Introduction |
| OAR | Open Air Ratio |
| OOB | Out-of-Band |
| ORv3 | Open Rack V3 |
| ORv3-HPR | Open Rack V3 High Power Rack (92KW+) |
| OSFP  | Octal Small Form Factor Pluggable  |
| PBMC | Pivoting Blind Mate Coupling  |
| PG25 | Propylene Glycol 25% |
| PMI | Power Management Interface |
| PON | Passive Optical Network |
| PVT | Production Verification Test |
| RDT | Reliability Demonstration Test |
| RMC | Rack Management Controller |
| RPU | Route Processor Unit |
| RTSW | Row Top-of-rack Switch |
| RunBMC | Meta modular BMC card standard |
| SCM | System Control Module |
| SDSW | Spine DSF Switch |
| SMB | Switch Main Board |
| STSW | Spine Top-of-rack Switch |
| SVS | SVS (vendor-specific voltage scaling) |
| UQD | Universal Quick Disconnect |

---
