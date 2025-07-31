/* Will this work? Idk. Why u asking me ;-;. I can't test it rn. I do have _some_ experience in C tho: https://github.com/exeme-project/exeme-lang
*/

// My saviour: https://www.dropbox.com/scl/fo/kpbelgbmg8rojmwv3n4z1/AOLMdmIAiLb8FsjjvZLMwZg/User%20Manual%20for%20Fingerprint%20Module?e=5&preview=R502-A+fingerprint+module+user+manual-V1.3.1.pdf&rlkey=cknh86c3k4btei2t4ugsyjimn&subfolder_nav_tracking=1&dl=0

#include <SoftwareSerial.h>

// Pin definitions
#define FINGERPRINT_WAKEUP_PIN PB2 // ATtiny85 Pin 7
#define NRF_BUTTON_PIN PB0		   // ATtiny85 Pin 5
#define FINGERPRINT_RX_PIN PB3	   // ATtiny85 Pin 2
#define FINGERPRINT_TX_PIN PB4	   // ATtiny85 Pin 3

// Packet identifiers
#define FINGERPRINT_CMD_PACKET 0x01
#define FINGERPRINT_ACK_PACKET 0x07
#define FINGERPRINT_DATA_PACKET 0x02
#define FINGERPRINT_END_PACKET 0x08

// Command codes
#define FINGERPRINT_GETIMAGE 0x01
#define FINGERPRINT_IMAGE2TZ 0x02
#define FINGERPRINT_SEARCH 0x04
#define FINGERPRINT_AUTOIDENTIFY 0x32

// Response codes
#define FINGERPRINT_OK 0x00
#define FINGERPRINT_NOFINGER 0x02

SoftwareSerial fserial(FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);

void setup() {
	// Init pins
	pinMode(FINGERPRINT_WAKEUP_PIN, INPUT_PULLUP);
	pinMode(NRF_BUTTON_PIN, OUTPUT);
	digitalWrite(NRF_BUTTON_PIN, HIGH); // Start with ze button released

	// Init fingerprint serial
	fserial.begin(57600);

	// Parar for module initialization
	delay(1000);
}

void loop() {
	// Check if finger is detected (IRQ pin goes low)
	if (digitalRead(FINGERPRINT_WAKEUP_PIN) == LOW) {
		delay(50); // Debounce
		if (digitalRead(FINGERPRINT_WAKEUP_PIN) == LOW) {
			// Use either simple authentication or auto-identify
			// simpleAuthentication();
			autoIdentify();
		}
	}
	delay(100);
}

void pressButton() {
	digitalWrite(NRF_BUTTON_PIN, LOW);	// Active low
	delay(50);							// Button press duration
	digitalWrite(NRF_BUTTON_PIN, HIGH); // Release button
	delay(1000);						// Prevent multiple triggers
}

// Way 1: Step-by-step authentication
void simpleAuthentication() {
	// Step 1: Get image
	if (sendCommand(FINGERPRINT_GETIMAGE, NULL, 0) != FINGERPRINT_OK) {
		return;
	}

	// Step 2: Convert image to template (store in buffer 1)
	uint8_t param1[] = {0x01}; // Buffer ID
	if (sendCommand(FINGERPRINT_IMAGE2TZ, param1, 1) != FINGERPRINT_OK) {
		return;
	}

	// Step 3: Search database
	// Parameters: BufferID (1), StartPage (0), PageNum (200)
	uint8_t param2[] = {0x01, 0x00, 0x00, 0xC8, 0x00};
	if (sendCommand(FINGERPRINT_SEARCH, param2, 5) == FINGERPRINT_OK) {
		pressButton();
	}
}

// Way 2: Using AutoIdentify command (simpler)
void autoIdentify() {
	// AutoIdentify command structure:
	// Header | Address | PacketID | Length | Cmd | Params | Checksum
	// EF 01 FF FF FF FF 01 00 08 32 [SecurityLevel] [StartID] [Num] [Config1] [Config2] [Checksum]
	// And a lotta magic numbers

	uint8_t packet[] = {
		0xEF,
		0x01, // Header
		0xFF,
		0xFF,
		0xFF,
		0xFF,					// Default address
		FINGERPRINT_CMD_PACKET, // Packet type
		0x00,
		0x08,					  // Length (8 bytes: cmd + params + checksum)
		FINGERPRINT_AUTOIDENTIFY, // Command
		0x03,					  // Security level (3)
		0x00,					  // Start at template 0
		0xC8,					  // Search 200 templates (0xC8 = 200)
		0x01,					  // Return status (1 = yes)
		0x01,					  // Max tries (1)
		0x00,
		0x00 // Checksum (placeholder)
	};

	// Calculate checksum (sum of bytes 6-14)
	uint16_t sum = 0;
	for (int i = 6; i <= 14; i++) {
		sum += packet[i];
	}
	packet[15] = sum >> 8;
	packet[16] = sum & 0xFF;

	// Send packet
	fserial.write(packet, sizeof(packet));

	// Freeeeeeeeeeeeeze for response
	if (readAckPacket() == FINGERPRINT_OK) {
		pressButton();
	}
}

uint8_t sendCommand(uint8_t cmd, uint8_t* params, uint8_t paramLen) {
	uint8_t packet[20]; // Max packet size we'll need (theoretically... ;-;, look i said this would be secure not... ok nevermind...)
	uint16_t sum = 0;

	// Build packet header (yes he can!)
	packet[0] = 0xEF; // Header
	packet[1] = 0x01;
	packet[2] = 0xFF; // Address
	packet[3] = 0xFF;
	packet[4] = 0xFF;
	packet[5] = 0xFF;
	packet[6] = FINGERPRINT_CMD_PACKET;

	// Packet length (cmd + params + checksum)
	uint16_t len = 1 + paramLen + 2;
	packet[7] = len >> 8;
	packet[8] = len & 0xFF;

	// Command
	packet[9] = cmd;
	sum += cmd;

	// Parameters
	for (uint8_t i = 0; i < paramLen; i++) {
		packet[10 + i] = params[i];
		sum += params[i];
	}

	// Checksum
	packet[10 + paramLen] = sum >> 8;
	packet[11 + paramLen] = sum & 0xFF;

	// Yeet packet
	fserial.write(packet, 12 + paramLen);

	// Read response
	return readAckPacket();
}

uint8_t readAckPacket() {
	uint8_t buffer[12];
	uint8_t idx = 0;
	uint32_t startTime = millis();

	// Wait for response with timeout (i dont have infinite time matey)
	while (idx < 12 && (millis() - startTime) < 2000) {
		if (fserial.available()) {
			buffer[idx++] = fserial.read();
		}
	}

	// Verify packet structure
	if (idx < 12 || buffer[0] != 0xEF || buffer[1] != 0x01 || // Header
		buffer[6] != FINGERPRINT_ACK_PACKET) {				  // ACK packet
		return 0xFF;										  // Error ;-;
	}

	// Verify checksum
	uint16_t sum = 0;
	for (uint8_t i = 6; i < 10; i++) {
		sum += buffer[i];
	}

	if (((sum >> 8) != buffer[10]) || ((sum & 0xFF) != buffer[11])) {
		return 0xFF; // Checksum error - i.e., f***
	}

	// Return confirmation code
	return buffer[9];
}