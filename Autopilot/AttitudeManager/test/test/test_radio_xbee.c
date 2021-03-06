/*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/

//-- unity: unit test framework
#include "unity.h"
#include <stdlib.h>
#include "./helpers/helpers.h"
#include "../../Drivers/Radio.h"
#include "../../Drivers/RadioXbee.h"
#include "mock_UART.h"
#include "mock_Logger.h"
#include <string.h>

/*******************************************************************************
 *    DEFINITIONS
 ******************************************************************************/
//make sure we're testing the xbee driver
#undef USE_RADIO
#define USE_RADIO RADIO_XBEE

/*******************************************************************************
 *    PRIVATE TYPES
 ******************************************************************************/

/*******************************************************************************
 *    PRIVATE DATA
 ******************************************************************************/

uint64_t destination_address = 0x123456789ABCDEFF;

/**
 * Helper struct for testing various AT command responses
 */
typedef struct XbeeATResponse {
    uint8_t* payload;
    uint16_t payload_length;
    uint8_t frame_id;
    char* at_command;
    uint8_t command_status;
    uint8_t checksum;
} XbeeATResponse;

/**
 * Helpful structure for testing RX responses
 */
typedef struct XbeeRXResponse {
    uint8_t* payload;
    uint16_t payload_length;
    uint64_t source_address;
    uint8_t checksum;
} XbeeRXResponse;

/*******************************************************************************
 *    PRIVATE FUNCTIONS
 ******************************************************************************/

/**
 * Builds an AT response that the xbee would send out via uart
 * @param response
 * @param make_checksum Whether to generate the correct checksum, or use the one defined in the struct
 * @param The length of the returned array
 * @return an array representing what the xbee will send out as an at response
 */
uint8_t* createATResponse(XbeeATResponse* response, bool make_checksum, uint16_t* length)
{
    uint16_t total_frame_size = 9 + response->payload_length;
    uint16_t total_payload_size = 5 + response->payload_length;
    uint8_t* to_return = malloc(total_frame_size);

    to_return[0] = XBEE_START_DELIMITER;

    //the length of the response
    to_return[1] = (total_payload_size & 0xFF00) >> 8;
    to_return[2] = total_payload_size & 0xFF;

    //frame type
    to_return[3] = XBEE_FRAME_TYPE_AT_RESPONSE;

    //frame id
    to_return[4] = response->frame_id;

    //at command code
    to_return[5] = response->at_command[0];
    to_return[6] = response->at_command[1];

    to_return[7] = response->command_status;

    //payload
    uint8_t i;
    for (i = 0; i < response->payload_length; i++) {
        to_return[8 + i] = response->payload[i];
    }

    //checksum
    //checksum
    if (!make_checksum) {
        to_return[total_frame_size - 1] = response->checksum;
    } else {
        to_return[total_frame_size - 1] = total_payload_size % 0xFF;
    }

    *length = total_frame_size;

    return to_return;
}

/**
 * Turns an XbeeRXResponse into an array that the xbee would output via uart
 * @param response
 * @param make_checksum Whether to generate the correct checksum, or use the one stored in the struct
 * @param The length of the returned array
 * @return 
 */
uint8_t* createRXResponse(XbeeRXResponse* response, bool make_checksum, uint16_t* length )
{
    uint16_t total_frame_size = 16 + response->payload_length;
    uint8_t* to_return = malloc(total_frame_size);
    int8_t i;

    to_return[0] = XBEE_START_DELIMITER;

    //the length of the response
    to_return[1] = ((12 + response->payload_length) & 0xFF00) >> 8;
    to_return[2] = (12 + response->payload_length) & 0xFF;

    //frame type
    to_return[3] = XBEE_FRAME_TYPE_RX_INDICATOR;

    //set the source address
    for (i = 7; i >= 0; i--) {
        to_return[4 + (7 - i)] = (response->source_address >> 8 * i) & 0xFF;
    }

    //reserved
    to_return[12] = 0xFF;
    to_return[13] = 0xFE;

    to_return[14] = 0; //receive options. Packet was acknowledged

    //payload
    for (i = 0; i < response->payload_length; i++) {
        to_return[15 + i] = response->payload[i];
    }

    //checksum
    if (!make_checksum) {
        to_return[total_frame_size - 1] = response->checksum;
    } else {
        to_return[total_frame_size - 1] = (12 + response->payload_length) % 0xFF;
    }
    *length = total_frame_size;
    return to_return;
}

uint8_t* createExpectedTXRequest(uint64_t address, uint8_t* payload, uint8_t payload_length)
{
    uint16_t total_frame_size = 18 + payload_length; //20 + 18 bytes
    uint8_t* expected = malloc(total_frame_size);
    int i;

    expected[0] = XBEE_START_DELIMITER;

    expected[1] = 0;
    expected[2] = 14 + payload_length; //14 byte overhead in the payload

    expected[3] = XBEE_FRAME_TYPE_TX_REQUEST;
    expected[4] = 0; //frame id should be 0 as we're not looking for a response

    for (i = 7; i >= 0; i--) {
        expected[5 + (7 - i)] = (address >> 8 * i) & 0xFF;
    }

    //reserved
    expected[13] = 0xFF;
    expected[14] = 0xFE;
    expected[15] = XBEE_BROADCAST_RADIUS;
    expected[16] = 0; //transmit options should be 0

    for (i = 0; i < payload_length; i++) {
        expected[17 + i] = payload[i];
    }

    expected[total_frame_size - 1] = (payload_length + 14) % 0xFF; //checksum
    return expected;
}

/*******************************************************************************
 *    SETUP, TEARDOWN
 ******************************************************************************/

void setUp(void)
{

}

void tearDown(void)
{
    clearRadioDownlinkQueue();
}

/*******************************************************************************
 *    TESTS
 ******************************************************************************/

void test_xbeeInitRadioShouldAskForDestinationAddress(void)
{
    uint8_t sb[8];
    uint8_t sb2[8];

    sb[0] = XBEE_START_DELIMITER;
    //length should be 4
    sb[1] = 0;
    sb[2] = 4;
    sb[3] = XBEE_FRAME_TYPE_AT_COMMAND;
    //frame id should be 1 since this is the first packet we queue
    sb[4] = 1;
    sb[5] = XBEE_AT_COMMAND_DESTINATION_ADDRESS_HIGH[0];
    sb[6] = XBEE_AT_COMMAND_DESTINATION_ADDRESS_HIGH[1];
    sb[7] = 4; //checksum
    int i;
    for (i = 0; i < 8; i++) {
        sb2[i] = sb[i];
    }

    sb2[4] = 2; //frame id should be 1 now
    sb2[5] = XBEE_AT_COMMAND_DESTINATION_ADDRESS_LOW[0];
    sb2[6] = XBEE_AT_COMMAND_DESTINATION_ADDRESS_LOW[1];

    info_Ignore();
    initUART_Expect(XBEE_UART_INTERFACE, XBEE_UART_BAUD_RATE, XBEE_UART_BUFFER_INITIAL_SIZE, XBEE_UART_BUFFER_MAX_SIZE, 3);
    initRadio();
    getTXSpace_IgnoreAndReturn(100); //give as much space as possible
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, sb, 8, 8); //queue the data for tranmission over UART
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, sb2, 8, 8); //queue the data for tranmission over UART
    TEST_ASSERT_TRUE(sendQueuedDownlinkPacket());
    TEST_ASSERT_TRUE(sendQueuedDownlinkPacket());
}

void test_sendQueuedDownlinkPacketShouldNotSendIfQueueIsEmpty(void)
{
    TEST_ASSERT_FALSE(sendQueuedDownlinkPacket());
}

void test_QueueDownlinkPacketShouldSendCorrectUARTMessageWithCorrectDestinationAddress(void)
{
    uint8_t payload[20];
    uint16_t payload_length = 20;
    uint16_t total_frame_size = 38; //20 + 18 bytes

    int i = 0;

    for (i = 0; i < 20; i++) {
        payload[i] = i + 1;
    }

    uint8_t* expected = createExpectedTXRequest(XBEE_BROADCAST_ADDRESS, payload, payload_length);

    getTXSpace_ExpectAndReturn(XBEE_UART_INTERFACE, 20 + 18); //give enough space
    TEST_ASSERT_TRUE(queueDownlinkPacket(payload, payload_length));
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, expected, total_frame_size, total_frame_size); //queue the data for tranmission over UART
    TEST_ASSERT_TRUE(sendQueuedDownlinkPacket());
}

void test_sendQueuedDownlinkPacketShouldNotSendIfUARTIsTooFull(void)
{
    uint8_t payload[20];
    uint16_t payload_length = 20;

    getTXSpace_ExpectAndReturn(XBEE_UART_INTERFACE, 20 + 18 - 1); //give 1 less than the required space
    TEST_ASSERT_TRUE(queueDownlinkPacket(payload, payload_length));
    TEST_ASSERT_FALSE(sendQueuedDownlinkPacket());
}

void test_QueueRadioStatusPacketShouldSendCorrectDataOverUart(void)
{
    uint8_t rssi[8];
    uint8_t tr[8];
    uint8_t re[8];

    rssi[0] = XBEE_START_DELIMITER;
    //length should be 4
    rssi[1] = 0;
    rssi[2] = 4;
    rssi[3] = XBEE_FRAME_TYPE_AT_COMMAND;
    //frame id should be 1 since this is the first packet we queue (after clear)
    rssi[4] = 1;
    rssi[5] = XBEE_AT_COMMAND_RSSI[0];
    rssi[6] = XBEE_AT_COMMAND_RSSI[1];
    rssi[7] = 4; //checksum

    int i;
    for (i = 0; i < 8; i++) {
        tr[i] = rssi[i];
        re[i] = rssi[i];
    }

    re[4] = 2; //frame id should be 1 now
    re[5] = XBEE_AT_COMMAND_RECEIVED_ERROR_COUNT[0];
    re[6] = XBEE_AT_COMMAND_RECEIVED_ERROR_COUNT[1];

    tr[4] = 3; //frame id should be 1 now
    tr[5] = XBEE_AT_COMMAND_TRANSMISSION_ERRORS[0];
    tr[6] = XBEE_AT_COMMAND_TRANSMISSION_ERRORS[1];

    getTXSpace_IgnoreAndReturn(100); //give it more than enough space
    getTXSpace_IgnoreAndReturn(100); //give it more than enough space
    getTXSpace_IgnoreAndReturn(100); //give it more than enough space
    queueRadioStatusPacket();
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, rssi, 8, 8); //queue the data for tranmission over UART
    TEST_ASSERT_TRUE(sendQueuedDownlinkPacket());
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, re, 8, 8); //queue the data for tranmission over UART
    TEST_ASSERT_TRUE(sendQueuedDownlinkPacket());
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, tr, 8, 8); //queue the data for tranmission over UART
    TEST_ASSERT_TRUE(sendQueuedDownlinkPacket());
}

void test_ParseUplinkPacketShouldReturnNullIfUARTEmpty(void)
{
    uint16_t length;
    getRXSize_ExpectAndReturn(XBEE_UART_INTERFACE, 0); //give too little space
    TEST_ASSERT_NULL(parseUplinkPacket(&length));
}

void test_ParseUplinkPacketShouldParseAndSendRssiATResponseCorrectly(void)
{
    XbeeATResponse rssi;
    uint16_t i;

    uint8_t payload[1];
    payload[0] = 34;
    rssi.at_command = XBEE_AT_COMMAND_RSSI;
    rssi.command_status = XBEE_AT_COMMAND_STATUS_OK;
    rssi.frame_id = 1;
    rssi.payload = payload;
    rssi.payload_length = 1;

    uint16_t length;
    uint16_t frame_length;
    uint8_t* expected = createATResponse(&rssi, true, &frame_length);

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command
    TEST_ASSERT_EQUAL_UINT8(payload[0], getRadioRSSI());
    free(expected);
}

void test_ParseUplinkPacketShouldParseAndSendTransmissionErrorsATResponseCorrectly(void)
{
    XbeeATResponse errors;
    uint16_t i;
    uint16_t transmission_errors = 34443;

    uint8_t payload[2];
    payload[0] = (transmission_errors & 0xFF00) >> 8;
    payload[1] = transmission_errors & 0xFF;
    errors.at_command = XBEE_AT_COMMAND_TRANSMISSION_ERRORS;
    errors.command_status = XBEE_AT_COMMAND_STATUS_OK;
    errors.frame_id = 1;
    errors.payload = payload;
    errors.payload_length = 2;

    uint16_t length;
    uint16_t frame_length;
    uint8_t* expected = createATResponse(&errors, true, &frame_length);

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command
    TEST_ASSERT_EQUAL_UINT8(transmission_errors, getRadioTransmissionErrors());
    free(expected);
}

void test_ParseUplinkPacketShouldParseAndReceiveErrorsATResponseCorrectly(void)
{
    XbeeATResponse errors;
    uint16_t i;
    uint16_t receive_errors = 32444;

    uint8_t payload[2];
    payload[0] = (receive_errors & 0xFF00) >> 8;
    payload[1] = receive_errors & 0xFF;
    errors.at_command = XBEE_AT_COMMAND_RECEIVED_ERROR_COUNT;
    errors.command_status = XBEE_AT_COMMAND_STATUS_OK;
    errors.frame_id = 1;
    errors.payload = payload;
    errors.payload_length = 2;

    uint16_t length;
    uint16_t frame_length;
    uint8_t* expected = createATResponse(&errors, true, &frame_length);

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command
    TEST_ASSERT_EQUAL_UINT8(receive_errors, getRadioReceiveErrors());
    free(expected);
}

void test_ParseUplinkPacketShouldParseAndSetDestinationAddressCorrectly(void)
{
    XbeeATResponse adress_low;
    XbeeATResponse adress_high;
    int i;

    uint8_t payload[8];

    //set the destination address
    for (i = 7; i >= 0; i--) {
        payload[(7 - i)] = (destination_address >> 8 * i) & 0xFF;
    }

    adress_high.at_command = XBEE_AT_COMMAND_DESTINATION_ADDRESS_HIGH;
    adress_low.at_command = XBEE_AT_COMMAND_DESTINATION_ADDRESS_LOW;
    adress_low.command_status = XBEE_AT_COMMAND_STATUS_OK;
    adress_high.command_status = XBEE_AT_COMMAND_STATUS_OK;

    adress_high.payload = payload;
    adress_high.payload_length = 4;
    adress_low.payload = &payload[4];
    adress_low.payload_length = 4;

    uint16_t length;
    uint16_t frame_length;
    uint8_t* expected_high = createATResponse(&adress_high, true, &frame_length);
    uint8_t* expected_low = createATResponse(&adress_low, true, &frame_length);

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected_high[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected_low[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command

    //test to make sure correct destination address is set by making a transmission
    uint8_t data[1];
    uint8_t* expected = createExpectedTXRequest(destination_address, data, 1);

    getTXSpace_ExpectAndReturn(XBEE_UART_INTERFACE, 100); //give enough space
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, expected, 19, 19); //queue the data for tranmission over UART
    queueDownlinkPacket(data, 1);
    sendQueuedDownlinkPacket();

    free(expected_high);
    free(expected_low);
}

void test_ParseUplinkPacketGivenInvalidATResponseForDestinationAddressHighShouldNotSetAddress(void)
{
    XbeeATResponse adress_high;
    int i;
    uint64_t new_address = 0xDDFFDDFFDDFFDDFF;

    uint8_t payload[8];

    //set the destination address
    for (i = 7; i >= 0; i--) {
        payload[(7 - i)] = (new_address >> 8 * i) & 0xFF;
    }

    adress_high.at_command = XBEE_AT_COMMAND_DESTINATION_ADDRESS_HIGH;
    adress_high.command_status = XBEE_AT_COMMAND_STATUS_OK;

    adress_high.payload = payload;
    adress_high.payload_length = 2; //this should be 4. 3 so that we trigger invalid

    uint16_t length;
    uint16_t frame_length;
    uint8_t* expected_high = createATResponse(&adress_high, true, &frame_length);

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected_high[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command

    //test to make sure destination address is not set by testing transmission
    uint8_t data[1];
    uint8_t* expected = createExpectedTXRequest(destination_address, data, 1);

    getTXSpace_ExpectAndReturn(XBEE_UART_INTERFACE, 100); //give enough space
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, expected, 19, 19); //queue the data for tranmission over UART
    queueDownlinkPacket(data, 1);
    sendQueuedDownlinkPacket();
    free(expected_high);
}

void test_ParseUplinkPacketGivenInvalidATResponseForDestinationAddressLowShouldNotSetAddress(void)
{
    XbeeATResponse adress_high;
    int i;
    uint64_t new_address = 0xDDFFDDFFDDFFDDFF;

    uint8_t payload[8];

    //set the destination address
    for (i = 7; i >= 0; i--) {
        payload[(7 - i)] = (new_address >> 8 * i) & 0xFF;
    }

    adress_high.at_command = XBEE_AT_COMMAND_DESTINATION_ADDRESS_LOW;
    adress_high.command_status = XBEE_AT_COMMAND_STATUS_OK;

    adress_high.payload = payload;
    adress_high.payload_length = 2; //this should be 4. 3 so that we trigger invalid

    uint16_t length;
    uint16_t frame_length;
    uint8_t* expected_high = createATResponse(&adress_high, true, &frame_length);

    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected_high[i]);
    }
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command

    //test to make sure destination address is not set by testing transmission
    uint8_t data[1];
    uint8_t* expected = createExpectedTXRequest(destination_address, data, 1);

    getTXSpace_ExpectAndReturn(XBEE_UART_INTERFACE, 100); //give enough space
    queueTXData_ExpectWithArray(XBEE_UART_INTERFACE, expected, 19, 19); //queue the data for tranmission over UART
    queueDownlinkPacket(data, 1);
    sendQueuedDownlinkPacket();
    free(expected_high);
}

void test_ParseUplinkPacketGivenValidRXIndicatorShouldReturnData(void)
{
    XbeeRXResponse rf;
    rf.source_address = destination_address;
    
    uint8_t payload[10];
    uint16_t payload_length = 10;
    uint16_t frame_length;
    int i;
    
    for (i = 0; i < payload_length; i ++){
        payload[i] = i;
    }
    rf.payload = payload;
    rf.payload_length = payload_length;
    
    uint8_t* expected = createRXResponse(&rf, true, &frame_length);
    
    getRXSize_IgnoreAndReturn(100); //give enough space

    for (i = 0; i < frame_length; i++) {
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, expected[i]);
    }
    
    uint16_t returned_length;
    uint8_t* returned = parseUplinkPacket(&returned_length);
    TEST_ASSERT_NOT_NULL_MESSAGE(returned, "Returned message should not be null");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(payload_length, returned_length, "Returned length should match that of payload");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(payload, returned,10, "Returned payload should match");
    
    free(expected);
}

void test_ParseUplinkPacketShouldDoNothingWithoutStartDelimiter(void)
{
    uint16_t length;
    int i;
    //for the first byte
    getRXSize_ExpectAndReturn(XBEE_UART_INTERFACE, 20); //give enough space
    readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, 0xEF);
    for (i = 19; i >0; i--) {
        getRXSize_ExpectAndReturn(XBEE_UART_INTERFACE, i); //give enough space
        readRXData_ExpectAndReturn(XBEE_UART_INTERFACE, 0xEF);
    }
    getRXSize_ExpectAndReturn(XBEE_UART_INTERFACE, i); //give enough space
    
    TEST_ASSERT_NULL(parseUplinkPacket(&length)); //we should still return null since its an AT command
}

