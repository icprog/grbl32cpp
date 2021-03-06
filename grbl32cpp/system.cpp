#include "system.h"

void System::init()
{
	
	/*
	CN CONFIGURATION AND OPERATION
	The CN pins are configured as follows:
	1.Disable CPU interrupts.
	2.Set desired CN I/O pin as input by setting corresponding TRISx register bits = 1.
	3.Enable the CN Module ON bit (CNCON<15>) = 1.
	4.Enable individual CN input pin(s), enable optional pull up(s) or pull down(s).
	5.Read corresponding PORTx registers to clear mismatch condition on CN input pins.
	6.Configure the CN interrupt priority bits, CNIP<2:0> (IPC6<20:18>), and subpriority bits CNIS<1:0> (IPC6<17:16>).
	7.Clear the CN interrupt flag bit, CNIF(IFS1<0>) = 0.
	8.Enable the CN interrupt enable bit, CNIE (IEC1<0>) = 1.
	9.Enable CPU interrupts.
	When a CN interrupt occurs, the user should read the PORTx register associated with the CN pin(s).
	This will clear the mismatch condition and set up the CN logic to detect the next pin change.
	The current PORTx value can be compared to the PORTx read value obtained at the last CN interrupt or during initialization,
	and used to determine which pin changed.
	The CN pins have a minimum input pulse-width specification.
	*/

	uint32_t intStatus = disableInterrupts();

	// enbale Change Notification interrupt
	CONTROL_PORT->TRISxSET.w = CONTROL_MASK; // input
	portADC->adxPcfg.set = CONTROL_MASK;	// those pins are analog too.
	if (bit_istrue(settings.flags, BITFLAG_HARD_LIMIT_ENABLE)) { // limit can be disabled
		portCN->cnPue.set = CONTROL_CN_MASK;	// pull-up
		portCN->cnEn.set = CONTROL_CN_MASK;	// enable pin for CN
	}
	CONTROL2_PORT->TRISxSET.w = CONTROL2_MASK; // input (stop & z-probe). not analog pin
	portCN->cnPue.set = CONTROL2_CN_MASK;
	portCN->cnEn.set = CONTROL2_CN_MASK;
	portCN->cnCon.set = B1 << 15;		// enable notification
	old_limit_port_value = CONTROL_PORT->PORTxbits.w;	// 1st read to clear mismatch
	setIntVector(_CHANGE_NOTICE_VECTOR, irq_change_notification);
	setIntPriority(_CHANGE_NOTICE_VECTOR, 4, 0);
	clearIntFlag(_CHANGE_NOTICE_IRQ);
	setIntEnable(_CHANGE_NOTICE_IRQ);
	restoreInterrupts(intStatus);

	// debug pin
	DEBUG_PORT->TRISxCLR.w = DEBUG_BIT;
	DEBUG_PORT->ODCxCLR.w = DEBUG_BIT;
	DEBUG_PORT->LATxCLR.w = DEBUG_BIT;

	//printStringln("System init");
}





// Returns if safety door is ajar(T) or closed(F), based on pin state.
//uint8_t system_check_safety_door_ajar()
//{
//#ifdef ENABLE_SAFETY_DOOR_INPUT_PIN
//#ifdef INVERT_CONTROL_PIN
//	return(bit_istrue(CONTROL_PIN, bit(SAFETY_DOOR_BIT)));
//#else
//	return(bit_isfalse(CONTROL_PIN, bit(SAFETY_DOOR_BIT)));
//#endif
//#else
//	return(false); // Input pin not enabled, so just return that it's closed.
//#endif
//}


// Executes user startup script, if stored.
void System::execute_startup(char *line)
{
	uint8_t n;
	for (n = 0; n < N_STARTUP_LINE; n++) {
		if (!(settings.settings_read_startup_line(n, line))) {
			report.status_message(STATUS_SETTING_READ_FAIL);
		}
		else {
			if (line[0] != 0) {
				printString(line); // Echo startup line to indicate execution.
				report.status_message(gc_execute_line(line));
			}
		}
	}
}


// Directs and executes one line of formatted input from protocol_process. While mostly
// incoming streaming g-code blocks, this also executes Grbl internal commands, such as 
// settings, initiating the homing cycle, and toggling switch states. This differs from
// the realtime command module by being susceptible to when Grbl is ready to execute the 
// next line during a cycle, so for switches like block delete, the switch only effects
// the lines that are processed afterward, not necessarily real-time during a cycle, 
// since there are motions already stored in the buffer. However, this 'lag' should not
// be an issue, since these commands are not typically used during a cycle.
uint8_t System::execute_line(char *line)
{
	uint8_t char_counter = 1;
	uint8_t helper_var = 0; // Helper variable
	float parameter, value;
	switch (line[char_counter]) {
	case 0: report.grbl_help(); break;
	case '$': case 'G': case 'C': case 'X':
		if (line[(char_counter + 1)] != 0) { return(STATUS_INVALID_STATEMENT); }
		switch (line[char_counter]) {
		case '$': // Prints Grbl settings
			if (sys.state & (STATE_CYCLE | STATE_HOLD)) { return(STATUS_IDLE_ERROR); } // Block during cycle. Takes too long to print.
			else { report.grbl_settings(); }
			break;
		case 'G': // Prints gcode parser state
			// TODO: Move this to realtime commands for GUIs to request this data during suspend-state.
			report.gcode_modes();
			break;
		case 'C': // Set check g-code mode [IDLE/CHECK]
			// Perform reset when toggling off. Check g-code mode should only work if Grbl
			// is idle and ready, regardless of alarm locks. This is mainly to keep things
			// simple and consistent.
			if (sys.state == STATE_CHECK_MODE) {
				mc_reset();
				report.report_feedback_message(MESSAGE_DISABLED);
			}
			else {
				if (sys.state) { return(STATUS_IDLE_ERROR); } // Requires no alarm mode.
				sys.state = STATE_CHECK_MODE;
				report.report_feedback_message(MESSAGE_ENABLED);
			}
			break;
		case 'X': // Disable alarm lock [ALARM]
			if (sys.state == STATE_ALARM) {
				report.report_feedback_message(MESSAGE_ALARM_UNLOCK);
				sys.state = STATE_IDLE;
				// Don't run startup script. Prevents stored moves in startup from causing accidents.
				//if (system_check_safety_door_ajar()) { // Check safety door switch before returning.
				//	bit_true(sys_rt_exec_state, EXEC_SAFETY_DOOR);
				//	protocol_execute_realtime(); // Enter safety door mode.
				//}
			} // Otherwise, no effect.
			break;
			//  case 'J' : break;  // Jogging methods
			// TODO: Here jogging can be placed for execution as a seperate subprogram. It does not need to be 
			// susceptible to other realtime commands except for e-stop. The jogging function is intended to
			// be a basic toggle on/off with controlled acceleration and deceleration to prevent skipped 
			// steps. The user would supply the desired feedrate, axis to move, and direction. Toggle on would
			// start motion and toggle off would initiate a deceleration to stop. One could 'feather' the
			// motion by repeatedly toggling to slow the motion to the desired location. Location data would 
			// need to be updated real-time and supplied to the user through status queries.
			//   More controlled exact motions can be taken care of by inputting G0 or G1 commands, which are 
			// handled by the planner. It would be possible for the jog subprogram to insert blocks into the
			// block buffer without having the planner plan them. It would need to manage de/ac-celerations 
			// on its own carefully. This approach could be effective and possibly size/memory efficient.  
			//       }
			//       break;
		}
		break;
	default:
		// Block any system command that requires the state as IDLE/ALARM. (i.e. EEPROM, homing)
		if (!(sys.state == STATE_IDLE || sys.state == STATE_ALARM)) { return(STATUS_IDLE_ERROR); }
		switch (line[char_counter]) {
		case '#': // Print Grbl NGC parameters
			if (line[++char_counter] != 0) { return(STATUS_INVALID_STATEMENT); }
			else { report.report_ngc_parameters(); }
			break;
		case 'H': // Perform homing cycle [IDLE/ALARM]
			if (bit_istrue(settings.flags, BITFLAG_HOMING_ENABLE)) {
				sys.state = STATE_HOMING; // Set system state variable
				// Only perform homing if Grbl is idle or lost.

				// TODO: Likely not required.
				//if (system_check_safety_door_ajar()) { // Check safety door switch before homing.
				//	bit_true(sys_rt_exec_state, EXEC_SAFETY_DOOR);
				//	protocol_execute_realtime(); // Enter safety door mode.
				//}


				mc_homing_cycle();
				if (!sys.abort) {  // Execute startup scripts after successful homing.
					sys.state = STATE_IDLE; // Set to IDLE when complete.
					st_go_idle(); // Set steppers to the settings idle state before returning.
					system_execute_startup(line);
				}
			}
			else { return(STATUS_SETTING_DISABLED); }
			break;
		case 'I': // Print or store build info. [IDLE/ALARM]
			if (line[++char_counter] == 0) {
				settings.read_build_info(line);
				report.build_info(line);
			}
			else { // Store startup line [IDLE/ALARM]
				if (line[char_counter++] != '=') { return(STATUS_INVALID_STATEMENT); }
				helper_var = char_counter; // Set helper variable as counter to start of user info line.
				do {
					line[char_counter - helper_var] = line[char_counter];
				} while (line[char_counter++] != 0);
				settings.store_build_info(line);
			}
			break;
		case 'R': // Restore defaults [IDLE/ALARM]
			if (line[++char_counter] != 'S') { return(STATUS_INVALID_STATEMENT); }
			if (line[++char_counter] != 'T') { return(STATUS_INVALID_STATEMENT); }
			if (line[++char_counter] != '=') { return(STATUS_INVALID_STATEMENT); }
			if (line[char_counter + 2] != 0) { return(STATUS_INVALID_STATEMENT); }
			switch (line[++char_counter]) {
			case '$': settings.restore(SETTINGS_RESTORE_DEFAULTS); break;
			case '#': settings.restore(SETTINGS_RESTORE_PARAMETERS); break;
			case '*': settings.restore(SETTINGS_RESTORE_ALL); break;
			default: return(STATUS_INVALID_STATEMENT);
			}
			report.feedback_message(MESSAGE_RESTORE_DEFAULTS);
			mc_reset(); // Force reset to ensure settings are initialized correctly.
			break;
		case 'N': // Startup lines. [IDLE/ALARM]
			if (line[++char_counter] == 0) { // Print startup lines
				for (helper_var = 0; helper_var < N_STARTUP_LINE; helper_var++) {
					if (!(settings.read_startup_line(helper_var, line))) {
						report.status_message(STATUS_SETTING_READ_FAIL);
					}
					else {
						report.startup_line(helper_var, line);
					}
				}
				break;
			}
			else { // Store startup line [IDLE Only] Prevents motion during ALARM.
				if (sys.state != STATE_IDLE) { return(STATUS_IDLE_ERROR); } // Store only when idle.
				helper_var = true;  // Set helper_var to flag storing method. 
				// No break. Continues into default: to read remaining command characters.
			}
		default:  // Storing setting methods [IDLE/ALARM]
			if (!read_float(line, &char_counter, &parameter)) { return(STATUS_BAD_NUMBER_FORMAT); }
			if (line[char_counter++] != '=') { return(STATUS_INVALID_STATEMENT); }
			if (helper_var) { // Store startup line
				// Prepare sending gcode block to gcode parser by shifting all characters
				helper_var = char_counter; // Set helper variable as counter to start of gcode block
				do {
					line[char_counter - helper_var] = line[char_counter];
				} while (line[char_counter++] != 0);
				// Execute gcode block to ensure block is valid.
				helper_var = gc_execute_line(line); // Set helper_var to returned status code.
				if (helper_var) { return(helper_var); }
				else {
					helper_var = trunc(parameter); // Set helper_var to int value of parameter
					settings.store_startup_line(helper_var, line);
				}
			}
			else { // Store global setting.
				if (!read_float(line, &char_counter, &value)) { return(STATUS_BAD_NUMBER_FORMAT); }
				if ((line[char_counter] != 0) || (parameter > 255)) { return(STATUS_INVALID_STATEMENT); }
				return(settings.store_global_setting((uint8_t)parameter, value));
			}
		}
	}
	return(STATUS_OK); // If '$' command makes it to here, then everything's ok.
}


// Returns machine position of axis 'idx'. Must be sent a 'step' array.
// NOTE: If motor steps and machine position are not in the same coordinate frame, this function
//   serves as a central place to compute the transformation.
float System::convert_axis_steps_to_mpos(int32_t *steps, uint8_t idx)
{
	float pos;
#ifdef COREXY
	if (idx == A_MOTOR) {
		pos = 0.5*((steps[A_MOTOR] + steps[B_MOTOR]) / settings.steps_per_mm[idx]);
	}
	else if (idx == B_MOTOR) {
		pos = 0.5*((steps[A_MOTOR] - steps[B_MOTOR]) / settings.steps_per_mm[idx]);
	}
	else {
		pos = steps[idx] / settings.steps_per_mm[idx];
	}
#else
	pos = steps[idx] / settings.steps_per_mm[idx];
#endif
	return(pos);
}


void System::convert_array_steps_to_mpos(float *position, int32_t *steps)
{
	uint8_t idx;
	for (idx = 0; idx<N_AXIS; idx++) {
		position[idx] = system_convert_axis_steps_to_mpos(steps, idx);
	}
	return;
}

