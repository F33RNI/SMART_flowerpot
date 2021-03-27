void pump_controller(void) {
	if (!pump_cycle) {
		if (check_time() && voltage > 6.0) {
			pump_cycle = CYCLES_NUM;
			Serial.print(F("Moisture 0 = "));
			Serial.print(moisture[0]);
			Serial.print(F("% "));
			Serial.print(F("Moisture 1 = "));
			Serial.print(moisture[1]);
			Serial.println(F("% "));
			Serial.print(F("Moisture 2 = "));
			Serial.print(moisture[2]);
			Serial.println(F("%"));
			Serial.println(F("[PUMP] Start pouring..."));
		}
	}
	else {
		if (millis() - pump_cycle_timer >= cycle_delay) {
			cycle_state = !cycle_state;
			if (cycle_state) {
				enable_pump(PUMP_0_PIN, 1);
				enable_pump(PUMP_1_PIN, 1);
				cycle_delay = CYCLE_DURATION;
			}
			else {
				enable_pump(PUMP_0_PIN, 0);
				enable_pump(PUMP_1_PIN, 0);
				cycle_delay = DELAY_BTW_CYCLES;
				pump_cycle--;
				if (!pump_cycle) {	// Finished
					weekday_poured = local_time->tm_wday;
					this_day_poured = 1;
					Serial.println(F("[PUMP] Pouring finished."));
				}
			}
			pump_cycle_timer = millis();
		}
	}
}

boolean check_time() {
	delay(500);
	if (current_state != 4) {
		time_temp_value = time_by_days[local_time->tm_wday];
		/*Serial.print("Current hour: ");
		Serial.println(local_time->tm_hour);
		Serial.print("Current weekday: ");
		Serial.println(local_time->tm_wday);
		Serial.print("Current weekday time: ");
		Serial.println(time_temp_value);
		Serial.println();*/
		if (time_temp_value > 0 && local_time->tm_hour >= time_temp_value && !(this_day_poured && weekday_poured == local_time->tm_wday)) {
			return true;
		}
	}
	return false;
}

void manual_pump(void) {
	Serial.println(F("[PUMP] Manual activation."));
	digitalWrite(LED_PIN, 0);
	for (uint8_t i = 0; i < CYCLES_NUM; i++)
	{
		enable_pump(PUMP_0_PIN, 1);
		enable_pump(PUMP_1_PIN, 1);
		delay(CYCLE_DURATION);
		enable_pump(PUMP_0_PIN, 0);
		enable_pump(PUMP_1_PIN, 0);
		delay(DELAY_BTW_CYCLES);
	}
	Serial.println(F("[PUMP] Pouring finished."));
	digitalWrite(LED_PIN, 1);
}

void enable_pump(uint8_t pin, boolean enable) {
	uint16_t pwm_value = mapfloat(voltage, 4, 14, PUMP_PWM_LOW_V, PUMP_PWM_HIG_V);
	if (enable) {
		Serial.print(F("[PUMP] Start pump on pin "));
		Serial.print(pin);
		Serial.print(F(" at "));
		Serial.println(pwm_value);
		for (uint16_t i = 0; i < pwm_value; i++)
		{
			analogWrite(pin, i);
			delay(2);
		}
		analogWrite(pin, pwm_value);

		// Record time
		//last_pump_time = local_time->tm_min + (local_time->tm_hour * 100) + (local_time->tm_mday * 10000) + (local_time->tm_mon * 1000000);
	}
	else
		analogWrite(pin, 0);
}