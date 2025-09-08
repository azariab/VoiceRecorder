	Main Project Requirements
    =========================
    • Build the ability to record sound from the 2 microphones installed on the main board
	• The sound should be combined into a stereo channel and saved in an audio file on the SD-Card in a dedicated folder
	• The audio should be stored in an uncompressed WAV, or even better, BWF file format (BWF should allow for timestamps)
	• The filename should reflect the exact creation date and time
	• The LCD should allow to record and play the files.
	• The small 2.5 inch LCD should have buttons for:
		○ Scroll through the files (arrows)
		○ Play the selected file
		○ Fast play the selected file upon long press
		○ Pause playing the selected file
		○ Delete the selected file
		○ Record a new file
		○ Stop recording
	• Since the LCD is small, it might have the option to switch between recording and playback screens to accommodate for all of those buttons 
	• The LCD should indicate the battery charge level
	• The LCD should have a button that takes you to a screen of debug prints and allows you to go back

References For Coding
=====================
	• Hardware Config: ESP32-S3-BOX-SENSOR
	• We will be using the peripherals: LCD, SD-Card, Battery Manager, Integrated Buttons, Bluetooth, WiFi, speaker
	• Schematics: 
		○ Main Board: https://github.com/espressif/esp-box/blob/master/hardware/SCH_ESP32-S3-BOX-3_V1.0/SCH_ESP32-S3-BOX-3-MB_V1.1_20230808.pdf
		○ "SENSOR" Docking Station: https://github.com/espressif/esp-box/blob/master/hardware/SCH_ESP32-S3-BOX-3_V1.0/SCH_ESP32-S3-BOX-3-SENSOR-01_V1.1_20230922.pdf
	• GitHub: https://github.com/espressif/esp-box
	• Code Examples: https://github.com/espressif/esp-box/tree/master/examples
