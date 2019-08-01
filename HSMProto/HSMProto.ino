#include <bluefruit.h>
#include <Codex.h>
#include <HSM.h>


/*
 * This function configures the adafruit feather. It is called automatically
 * on startup.
 */
void setup(void) {
    initConsole();
    initHSM();
    initBluetooth();
    suspendLoop();  // empty anyway
}


/*
 * This function is called repeatedly once the adafruit feather has been setup.
 */
void loop(void) {
    // Everything is handled by event callbacks
}


/*
 * This function initializes the serial console if one exists.
 */
void initConsole() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("The ButtonUp™ Console");
    Serial.println("---------------------");
    Serial.println("");
}


/*
 * The hardware security module (HSM) encapsulates and protects the private key
 * and implements all the required public-private cryptographic functions.
 */
HSM* hsm;


/*
 * This function initializes the hardware security module (HSM).
 */
void initHSM() {
    Serial.println("Initializing the hardware security module (HSM)...");
    hsm = new HSM();
    Serial.println("Done.");
    Serial.println("");
}


// Bluetooth Services
const uint16_t BLOCK_SIZE = 510;  // max MTU size minus first two bytes
BLEDis  bledis;  // device information service
BLEUart bleuart(BLOCK_SIZE + 3); // UART communication service (FIFO size)


/*
 * This function initializes the low energy bluetooth processor.
 */
void initBluetooth() {
    Serial.println("Initializing the bluetooth module...");

    // Setup the BLE LED to be enabled on CONNECT.
    Bluefruit.autoConnLed(true);

    // Config the peripheral connection
    // Note: Maximum bandwith requires more SRAM.
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

    Bluefruit.begin();

    // Limit the range for connections for better security
    // Allowed values: -40, -20, -16, -12, -8, -4, 0, and 4
    Bluefruit.setTxPower(-20);

    // The name will be displayed in the mobile app
    Bluefruit.setName("ButtonUp™");

    // Add callbacks for important events
    Bluefruit.Periph.setConnectCallback(connectCallback);
    Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

    // Configure and start the Device Information Service
    bledis.setManufacturer("Adafruit Industries");
    bledis.setModel("Bluefruit Feather nRF52");
    bledis.begin();
  
    // Configure and start UART Communication Service
    bleuart.begin();
    bleuart.setRxCallback(requestCallback);
    bleuart.setNotifyCallback(notifyCallback);

    // Configure and start advertising for the device
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    //Bluefruit.Advertising.addTxPower();              // uses the current BLE power level
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.Advertising.addName();                 // uses the device name specified above
    Bluefruit.Advertising.restartOnDisconnect(true); // auto advertising when disconnected
    Bluefruit.Advertising.setInterval(32, 244);      // fast mode and slow mode (in units of 0.625 ms)
    Bluefruit.Advertising.setFastTimeout(30);        // timeout in seconds for fast mode, then slow mode
    Bluefruit.Advertising.start(0);                  // 0 = Don't stop advertising after N seconds  

    Serial.println("Done.");
    Serial.println("");
}


/*
 * This callback function is invoked each time a connection to the device occurs.
 */
void connectCallback(uint16_t connectionHandle) {
    BLEConnection* connection = Bluefruit.Connection(connectionHandle);
    char peerName[64] = { 0 };
    connection->getPeerName(peerName, 64);
    Serial.print("Connected to ");
    Serial.println(peerName);
    Serial.println("");
}


/*
 * This callback function is invoked each time a connection to the device is lost.
 */
void disconnectCallback(uint16_t connectionHandle, uint8_t reason) {
    Serial.print("Disconnected from mobile device - reason code: 0x");
    Serial.println(reason, HEX);
    Serial.println("See: ~/Library/Arduino15/packages/adafruit/hardware/nrf52/0.11.1/cores/nRF5/nordic/softdevice/s140_nrf52_6.1.1_API/include/ble_hci.h");
    Serial.println("");
}


/*
 * The request buffer is used to hold all of the information associated with one
 * or more requests that are received from a paired mobile device. For efficiency,
 * the arguments are referenced inline in the buffer rather than being copied into
 * their own memory.
 */
const int BUFFER_SIZE = 4096;
uint8_t buffer[BUFFER_SIZE];
uint8_t requestType;
size_t requestSize = 0;


/*
 * This structure is used to reference the section of the request buffer that
 * contains a specific argument that was passed as part of the request. Each
 * request has the following byte format:
 *   Request (1 byte) [0..255]
 *   Number of Arguments (1 byte) [0..255]
 *   Length of Argument 1 (2 bytes) [0..65535]
 *   Argument 1 ([0..65535] bytes)
 *   Length of Argument 2 (2 bytes) [0..65535]
 *   Argument 2 ([0..65535] bytes)
 *      ...
 *   Length of Argument N (2 bytes) [0..65535]
 *   Argument N ([0..65535] bytes)
 *
 * If the entire request is only a single byte long then the number of arguments
 * is assumed to be zero.
 */
struct Argument {
    uint8_t* pointer;
    size_t length;
};
Argument* arguments = 0;
size_t numberOfArguments = 0;


// Forward Declarations
bool readRequest(uint16_t connectionHandle);
void writeResult(uint16_t connectionHandle, bool result);
void writeResult(uint16_t connectionHandle, uint8_t* result, size_t length);
void writeError(uint16_t connectionHandle);


/*
 * This callback function is invoked each time a request is received by the BLE UART.
 */
void requestCallback(uint16_t connectionHandle) {
    // read the next request from the mobile device
    if (!readRequest(connectionHandle)) {
        Serial.println("Unable to read the request.");
        writeError(connectionHandle);
        return;
    }
    Serial.print("Request: ");

    switch (requestType) {
        case 0: {
            Serial.println("Load Block");
            writeResult(connectionHandle, true);
            Serial.println("Succeeded");
            Serial.println("");
            break;
        }

        case 1: {
            Serial.println("Digest Bytes");
            boolean success = false;
            const uint8_t* bytes = arguments[0].pointer;
            const size_t size = arguments[0].length;
            const uint8_t* digest = hsm->digestBytes(bytes, size);
            if (digest) {
                const char* encoded = Codex::encode(digest, DIG_SIZE);
                Serial.print("Bytes Digest: ");
                Serial.println(encoded);
                delete [] encoded;
                success = true;
                writeResult(connectionHandle, digest, DIG_SIZE);
                delete [] digest;
            }
            if (!success) writeError(connectionHandle);
            Serial.println(success ? "Succeeded" : "Failed");
            Serial.println("");
            break;
        }

        case 2: {
            boolean success = false;
            if (numberOfArguments > 0 && numberOfArguments < 3 && arguments[0].length == KEY_SIZE) {
                const uint8_t* publicKey;
                uint8_t* newSecretKey = arguments[0].pointer;
                if (numberOfArguments == 2 && arguments[1].length == KEY_SIZE) {
                    Serial.println("Regenerate Keys");
                    uint8_t* existingSecretKey = arguments[1].pointer;
                    publicKey = hsm->generateKeys(newSecretKey, existingSecretKey);
                    memset(existingSecretKey, 0x00, KEY_SIZE);
                } else {
                    Serial.println("Generate Keys");
                    publicKey = hsm->generateKeys(newSecretKey);
                }
                if (publicKey) {
                    success = true;
                    const char* encoded = Codex::encode(publicKey, KEY_SIZE);
                    Serial.print("Public Key: ");
                    Serial.println(encoded);
                    delete [] encoded;
                    writeResult(connectionHandle, publicKey, KEY_SIZE);
                    delete [] publicKey;
                }
                memset(newSecretKey, 0x00, KEY_SIZE);
            }
            if (!success) writeError(connectionHandle);
            Serial.println(success ? "Succeeded" : "Failed");
            Serial.println("");
            break;
        }

        case 3: {
            Serial.println("Sign Bytes");
            boolean success = false;
            uint8_t* secretKey = arguments[0].pointer;
            if (arguments[0].length == KEY_SIZE) {
                const uint8_t* bytes = arguments[1].pointer;
                const size_t size = arguments[1].length;
                const uint8_t* signature = hsm->signBytes(secretKey, bytes, size);
                if (signature) {
                    success = true;
                    Serial.print("Signature: ");
                    Serial.println(Codex::encode(signature, SIG_SIZE));
                    writeResult(connectionHandle, signature, SIG_SIZE);
                    delete [] signature;
                }
            }
            memset(secretKey, 0x00, KEY_SIZE);
            if (!success) writeError(connectionHandle);
            Serial.println(success ? "Succeeded" : "Failed");
            Serial.println("");
            break;
        }

        case 4: {
            Serial.println("Valid Signature?");
            boolean success = false;
            const uint8_t* bytes = arguments[0].pointer;
            const size_t size = arguments[0].length;
            uint8_t* signature = arguments[1].pointer;
            if (arguments[1].length == SIG_SIZE) {
                if (numberOfArguments == 3) {
                    uint8_t* aPublicKey = arguments[2].pointer;
                    if (arguments[2].length == KEY_SIZE) {
                        success = hsm->validSignature(bytes, size, signature, aPublicKey);
                    }
                } else {
                    success = hsm->validSignature(bytes, size, signature);
                }
            }
            writeResult(connectionHandle, success);
            Serial.println(success ? "Succeeded" : "Failed");
            Serial.println("");
            break;
        }

        case 5: {
            Serial.println("Erase Keys");
            boolean success = hsm->eraseKeys();
            writeResult(connectionHandle, success);
            Serial.println(success ? "Succeeded" : "Failed");
            Serial.println("");
            break;
        }

        default: {
            Serial.print("Invalid request type (");
            Serial.print(requestType);
            Serial.println("), try again...");
            Serial.println("");
            writeError(connectionHandle);
            break;
        }
    }

}


/*
 * This callback function is invoked each time notification is enabled or disabled.
 */
void notifyCallback(uint16_t connectionHandle, bool enabled) {
}


/*
 * This function generates a new byte array containing the specified number of
 * random bytes.
 */
uint8_t* randomBytes(size_t length) {
    uint8_t* bytes = new uint8_t[length];
    for (size_t i = 0; i < length; i++) {
        bytes[i] = (uint8_t) random(256);
    }
    return bytes;
}


/*
 * This function reads the next request from the BLE UART and its arguments from the request
 * buffer and indexes the arguments to make them easy to access. Each request has one of the
 * following formats:
 *
 * One Block of a Large Request (greater than 512 bytes)
 * [    1 byte    ][      1 byte       ][     510 bytes      ]
 * [     0xFF     ][ the block number  ][ one block of bytes ]
 *
 * The First Block of a Complete Request
 * [    1 byte    ][      1 byte       ][  510 bytes   ]
 * [ request type ][number of arguments][block of bytes]
 *
 * The buffer containing the full request has the form:
 * [      2 bytes       ][ size of argument 1 ][      2 bytes       ][ size of argument 2 ]...
 * [ size of argument 1 ][  argument 1 bytes  ][ size of argument 2 ][  argument 2 bytes  ]...
 * 
 */
bool readRequest(uint16_t connectionHandle) {
    Serial.println("Attempting to read...");

    // Read in the request information
    requestType = bleuart.read();
    if (requestType == 0) {
        // It's an extended sized request so load in one block of it
        uint8_t blockNumber = bleuart.read();
        Serial.print("Block number: ");
        Serial.println(blockNumber);
        requestSize += bleuart.read(buffer + blockNumber * BLOCK_SIZE, BLOCK_SIZE);
    } else {
        // It's a full request so parse it
        numberOfArguments = bleuart.read();
        requestSize += bleuart.read(buffer, BLOCK_SIZE);
        size_t index = 0;
        if (arguments) delete [] arguments;
        arguments = new Argument[numberOfArguments];
        for (size_t i = 0; i < numberOfArguments; i++) {
            uint16_t numberOfBytes = buffer[index++] << 8 | buffer[index++];
            arguments[i].pointer = buffer + index;
            arguments[i].length = numberOfBytes;
            index += numberOfBytes;
        }
        if (requestSize > BUFFER_SIZE || requestSize != index) {
            Serial.print("Invalid request length: ");
            Serial.println(requestSize);
            Serial.print("Expected: ");
            Serial.println(index);
            requestSize = 0;
            return false;
        }
        requestSize = 0;
    }
    if (requestSize > BUFFER_SIZE) {
        Serial.print("The request length is too long: ");
        Serial.println(requestSize);
        requestSize = 0;
        return false;
    }
    return true;
}


/*
 * This function writes the result of a request as a boolean value to the BLE UART.
 */
void writeResult(uint16_t connectionHandle, bool result) {
    bleuart.write(result ? 0x01 : 0x00);
    bleuart.flush();
}


/*
 * This function writes the result of a request as a byte array value to the BLE UART.
 */
void writeResult(uint16_t connectionHandle, const uint8_t* result, size_t length) {
    bleuart.write(result, length);
    bleuart.flush();
}


/*
 * This function writes the error result of a request to the BLE UART.
 */
void writeError(uint16_t connectionHandle) {
    bleuart.write(0xFF);
    bleuart.flush();
}

