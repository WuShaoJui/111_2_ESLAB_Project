#include "mbed.h"
#include "stm32l475e_iot01_audio.h"
#include <cstdint>
#include <cstdio>
#include <vector>
#include <events/mbed_events.h>
#include "ble/BLE.h"
#include "ble/gap/Gap.h"
#include "mbed-trace/mbed_trace.h"
#include "ClapService.h"
#include "pretty_printer.h"

// BLE
const static char DEVICE_NAME[] = "Fake device";
static events::EventQueue event_queue(/* event count */ 16 * EVENTS_EVENT_SIZE);

static uint16_t PCM_Buffer[PCM_BUFFER_LEN/2];
static BSP_AUDIO_Init_t MicParams;

static DigitalOut led(LED1);
static EventQueue ev_queue;

// Place to store final audio (alloc on the heap), here two seconds...
static size_t TARGET_AUDIO_BUFFER_NB_SAMPLES = AUDIO_SAMPLING_FREQUENCY * 1;
static int16_t *TARGET_AUDIO_BUFFER = (int16_t*)calloc(TARGET_AUDIO_BUFFER_NB_SAMPLES, sizeof(int16_t));
static size_t TARGET_AUDIO_BUFFER_IX = 0;

static size_t SHORT_TERM_MAX = 10;
static uint16_t *MAX_DATA = (uint16_t*)calloc(SHORT_TERM_MAX, sizeof(int16_t));
static size_t MAX_DATA_IX = 0;

// 1 event = 2 ms
static size_t SKIP_FIRST_EVENTS = 100; // skip 0.2 s to not record the button click
static size_t half_transfer_events = 0;
static size_t transfer_complete_events = 0;

// flag for test
static uint8_t detected = 0;
static int amp_max = 0;
static int amp_min = 0;

// clap detection param
uint32_t sta = 0;     // short term average
uint32_t stm = 0;     // short term max
uint32_t st_a = 20;
uint32_t st_m = 16;
uint32_t lt = 500;
uint32_t ltm = 0;
uint32_t macd = 512;  // max allowed clap duration
uint32_t threshold_const = 4500;
uint32_t threshold = 0;
uint32_t max_val = 0;
uint32_t duration = 0;
uint32_t exceed_threshold = 0;
uint32_t clap_l = 0;
uint32_t next_i = 0;

// BLE
class Clap_Service : ble::Gap::EventHandler {
public:
    Clap_Service(BLE &ble, events::EventQueue &event_queue) :
        _ble(ble),
        _event_queue(event_queue),
        _led1(LED1, 1),
        _clap_service(NULL),
        _clap_uuid(ClapService::CLAP_SERVICE_UUID),
        _adv_data_builder(_adv_buffer)
    {
    }

    void start()
    {
        _ble.init(this, &Clap_Service::on_init_complete);

        _event_queue.dispatch_forever();
    }

private:
    /** Callback triggered when the ble initialization process has finished */
    void on_init_complete(BLE::InitializationCompleteCallbackContext *params)
    {
        if (params->error != BLE_ERROR_NONE) {
            printf("Ble initialization failed.");
            return;
        }

        print_mac_address();

        _clap_service = new ClapService(_ble, 0);

        /* this allows us to receive events like onConnectionComplete() */
        _ble.gap().setEventHandler(this);

        _event_queue.call_every(500ms, this, &Clap_Service::blink);

        start_advertising();
    }

    void start_advertising()
    {
        /* Create advertising parameters and payload */

        ble::AdvertisingParameters adv_parameters(
            ble::advertising_type_t::CONNECTABLE_UNDIRECTED,
            ble::adv_interval_t(ble::millisecond_t(100))
        );

        _adv_data_builder.setFlags();
        _adv_data_builder.setAppearance(ble::adv_data_appearance_t::GENERIC_HEART_RATE_SENSOR);
        _adv_data_builder.setLocalServiceList({&_heartrate_uuid, 1});
        _adv_data_builder.setLocalServiceList({&_clap_uuid, 1});
        _adv_data_builder.setName(DEVICE_NAME);

        /* Setup advertising */

        ble_error_t error = _ble.gap().setAdvertisingParameters(
            ble::LEGACY_ADVERTISING_HANDLE,
            adv_parameters
        );

        if (error) {
            printf("_ble.gap().setAdvertisingParameters() failed\r\n");
            return;
        }

        error = _ble.gap().setAdvertisingPayload(
            ble::LEGACY_ADVERTISING_HANDLE,
            _adv_data_builder.getAdvertisingData()
        );

        if (error) {
            printf("_ble.gap().setAdvertisingPayload() failed\r\n");
            return;
        }

        /* Start advertising */

        error = _ble.gap().startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);

        if (error) {
            printf("_ble.gap().startAdvertising() failed\r\n");
            return;
        }

        printf("Clap service advertising, please connect\r\n");
    }

    void clap_detected(void) {
        printf("Transmit detected @ %d\r\n", this->_time);
        _event_queue.call(Callback<void(int)>(_clap_service, &ClapService::updateClapTime), this->_time);
    }

    void blink(void) {
        _led1 = !_led1;
    }

    int start_clap_detect();

    void clap_detect();

    /* these implement ble::Gap::EventHandler */
private:
    /* when we connect we stop advertising, restart advertising so others can connect */
    virtual void onConnectionComplete(const ble::ConnectionCompleteEvent &event)
    {
        if (event.getStatus() == ble_error_t::BLE_ERROR_NONE) {
            printf("Client connected, you may now subscribe to updates\r\n");
        }

        start_clap_detect();
        
    }

    /* when we connect we stop advertising, restart advertising so others can connect */
    virtual void onDisconnectionComplete(const ble::DisconnectionCompleteEvent &event)
    {
        printf("Client disconnected, restarting advertising\r\n");

        ble_error_t error = _ble.gap().startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);

        if (error) {
            printf("_ble.gap().startAdvertising() failed\r\n");
            return;
        }
    }

private:
    BLE &_ble;
    events::EventQueue &_event_queue;

    // heart rate
    UUID _heartrate_uuid;

    // botton
    DigitalOut  _led1;
    ClapService *_clap_service;
    UUID _clap_uuid;

    int _time;

    uint8_t _adv_buffer[ble::LEGACY_ADVERTISING_MAX_SIZE];
    ble::AdvertisingDataBuilder _adv_data_builder;

};

void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context)
{
    event_queue.call(Callback<void()>(&context->ble, &BLE::processEvents));
}

// callback that gets invoked when TARGET_AUDIO_BUFFER is full
// void target_audio_buffer_full() {
//     // pause audio stream
//     int32_t ret = BSP_AUDIO_IN_Pause(AUDIO_INSTANCE);
//     if (ret != BSP_ERROR_NONE) {
//         printf("Error Audio Pause (%d)\n", ret);
//     }
//     else {
//         printf("OK Audio Pause\n");
//     }

//     // create WAV file
//     size_t wavFreq = AUDIO_SAMPLING_FREQUENCY;
//     size_t dataSize = (TARGET_AUDIO_BUFFER_NB_SAMPLES * 2);
//     size_t fileSize = 44 + (TARGET_AUDIO_BUFFER_NB_SAMPLES * 2);

//     uint8_t wav_header[44] = {
//         0x52, 0x49, 0x46, 0x46, // RIFF
//         (uint8_t)(fileSize & 0xff), (uint8_t)((fileSize >> 8) & 0xff), (uint8_t)((fileSize >> 16) & 0xff), (uint8_t)((fileSize >> 24) & 0xff),
//         0x57, 0x41, 0x56, 0x45, // WAVE
//         0x66, 0x6d, 0x74, 0x20, // fmt
//         0x10, 0x00, 0x00, 0x00, // length of format data
//         0x01, 0x00, // type of format (1=PCM)
//         0x01, 0x00, // number of channels
//         (uint8_t)(wavFreq & 0xff), (uint8_t)((wavFreq >> 8) & 0xff), (uint8_t)((wavFreq >> 16) & 0xff), (uint8_t)((wavFreq >> 24) & 0xff),
//         0x00, 0x7d, 0x00, 0x00, // 	(Sample Rate * BitsPerSample * Channels) / 8
//         0x02, 0x00, 0x10, 0x00,
//         0x64, 0x61, 0x74, 0x61, // data
//         (uint8_t)(dataSize & 0xff), (uint8_t)((dataSize >> 8) & 0xff), (uint8_t)((dataSize >> 16) & 0xff), (uint8_t)((dataSize >> 24) & 0xff),
//     };

//     printf("Total complete events: %lu, index is %lu\n", transfer_complete_events, TARGET_AUDIO_BUFFER_IX);

//     // print both the WAV header and the audio buffer in HEX format to serial
//     // you can use the script in `hex-to-buffer.js` to make a proper WAV file again
//     // printf("WAV file:\n");
//     // for (size_t ix = 0; ix < 44; ix++) {
//     //     printf("%02x", wav_header[ix]);
//     // }

//     // uint8_t *buf = (uint8_t*)TARGET_AUDIO_BUFFER;
//     // for (size_t ix = 0; ix < TARGET_AUDIO_BUFFER_NB_SAMPLES * 2; ix++) {
//     //     printf("%02x", buf[ix]);
//     // }
//     // printf("\n");
// }

/**
* @brief  Half Transfer user callback, called by BSP functions.
* @param  None
* @retval None
*/
void BSP_AUDIO_IN_HalfTransfer_CallBack(uint32_t Instance) {
    half_transfer_events++;
    if (half_transfer_events <= SKIP_FIRST_EVENTS) return;

    uint32_t buffer_size = PCM_BUFFER_LEN / 2; /* Half Transfer */
    uint32_t nb_samples = buffer_size / sizeof(int16_t); /* Bytes to Length */

    if ((TARGET_AUDIO_BUFFER_IX + nb_samples) > TARGET_AUDIO_BUFFER_NB_SAMPLES) {
        return;
    }

    /* Copy first half of PCM_Buffer from Microphones onto Fill_Buffer */
    memcpy(((uint8_t*)TARGET_AUDIO_BUFFER) + (TARGET_AUDIO_BUFFER_IX * 2), PCM_Buffer, buffer_size);
    TARGET_AUDIO_BUFFER_IX += nb_samples;

    for(int i = 0; i < nb_samples; ++i){
        int amp = PCM_Buffer[i];
        amp = amp >= 32768 ? (amp - 65536) : amp;
        if(amp > amp_max) amp_max = amp;
        if(amp < amp_min) amp_min = amp;
        amp = amp < 0 ? -amp : amp;
        if(amp >= 8000) detected = 1;
    }

    if (TARGET_AUDIO_BUFFER_IX >= TARGET_AUDIO_BUFFER_NB_SAMPLES) {
        TARGET_AUDIO_BUFFER_IX = 0;
        return;
    }
}

/**
* @brief  Transfer Complete user callback, called by BSP functions.
* @param  None
* @retval None
*/
void BSP_AUDIO_IN_TransferComplete_CallBack(uint32_t Instance) {
    transfer_complete_events++;
    if (transfer_complete_events <= SKIP_FIRST_EVENTS) return;

    uint32_t buffer_size = PCM_BUFFER_LEN / 2; /* Half Transfer */
    uint32_t nb_samples = buffer_size / sizeof(int16_t); /* Bytes to Length */

    if ((TARGET_AUDIO_BUFFER_IX + nb_samples) > TARGET_AUDIO_BUFFER_NB_SAMPLES) {
        return;
    }

    /* Copy second half of PCM_Buffer from Microphones onto Fill_Buffer */
    memcpy(((uint8_t*)TARGET_AUDIO_BUFFER) + (TARGET_AUDIO_BUFFER_IX * 2),
        ((uint8_t*)PCM_Buffer) + (nb_samples * 2), buffer_size);
    TARGET_AUDIO_BUFFER_IX += nb_samples;

    for(int i = nb_samples; i < nb_samples * 2; ++i){
        int amp = PCM_Buffer[i];
        amp = amp >= 32768 ? (amp - 65536) : amp;
        if(amp > amp_max) amp_max = amp;
        if(amp < amp_min) amp_min = amp;
        amp = amp < 0 ? -amp : amp;
        if(amp >= 8000) detected = 1;
    }

    if (TARGET_AUDIO_BUFFER_IX >= TARGET_AUDIO_BUFFER_NB_SAMPLES) {
        TARGET_AUDIO_BUFFER_IX = 0;
        // ev_queue.call(&target_audio_buffer_full);
        return;
    }
}

/**
  * @brief  Manages the BSP audio in error event.
  * @param  Instance Audio in instance.
  * @retval None.
  */
void BSP_AUDIO_IN_Error_CallBack(uint32_t Instance) {
    printf("BSP_AUDIO_IN_Error_CallBack\n");
}

void print_stats() {
    printf("Half %lu, Complete %lu, IX %lu\n", half_transfer_events, transfer_complete_events,
        TARGET_AUDIO_BUFFER_IX);
}

void start_recording() {
    int32_t ret;
    uint32_t state;

    ret = BSP_AUDIO_IN_GetState(AUDIO_INSTANCE, &state);
    uint32_t *temp;
    // BSP_AUDIO_IN_GetDevice(AUDIO_INSTANCE, temp);
    // printf("GET dev(%d)\n", *temp);
    if (ret != BSP_ERROR_NONE) {
        printf("Cannot start recording: Error getting audio state (%d)\n", ret);
        return;
    }
    if (state == AUDIO_IN_STATE_RECORDING) {
        printf("Cannot start recording: Already recording\n");
        // target_audio_buffer_full();
        return;
    }

    // reset audio buffer location
    TARGET_AUDIO_BUFFER_IX = 0;
    transfer_complete_events = 0;
    half_transfer_events = 0;

    ret = BSP_AUDIO_IN_Record(AUDIO_INSTANCE, (uint8_t *) PCM_Buffer, PCM_BUFFER_LEN);
    if (ret != BSP_ERROR_NONE) {
        printf("Error Audio Record (%ld)\n", ret);
        return;
    }
    else {
        printf("OK Audio Record\n");
    }
    
}

int Clap_Service::start_clap_detect(){
    if (!TARGET_AUDIO_BUFFER) {
        printf("Failed to allocate TARGET_AUDIO_BUFFER buffer\n");
        return 0;
    }

    // set up the microphone
    MicParams.BitsPerSample = 16;
    MicParams.ChannelsNbr = AUDIO_CHANNELS;
    MicParams.Device = AUDIO_IN_DIGITAL_MIC1;
    MicParams.SampleRate = AUDIO_SAMPLING_FREQUENCY;
    MicParams.Volume = 32;

    int32_t ret = BSP_AUDIO_IN_Init(AUDIO_INSTANCE, &MicParams);

    if (ret != BSP_ERROR_NONE) {
        printf("Error Audio Init (%ld)\r\n", ret);
        return 1;
    } else {
        printf("OK Audio Init\t(Audio Freq=%ld)\r\n", AUDIO_SAMPLING_FREQUENCY);
    }

    printf("Init data...\n");
    for(int i = 0; i < TARGET_AUDIO_BUFFER_NB_SAMPLES; ++i) TARGET_AUDIO_BUFFER[i] = 0;
    for(int i = 0; i < SHORT_TERM_MAX; ++i) MAX_DATA[i] = 0;

    printf("Press the BLUE button to record a message\n");


    // hit the blue button to record a message
    static InterruptIn btn(BUTTON1);
    btn.fall(event_queue.event(&start_recording));

    
    this->_event_queue.call_every(1ms, this, &Clap_Service::clap_detect);

    return 0;
}

void Clap_Service::clap_detect(){
    int i = TARGET_AUDIO_BUFFER_IX-1;
    if(i == -1) i = AUDIO_SAMPLING_FREQUENCY-1;
    // if(i%16000 == 0) printf("Time : %f s\n", (transfer_complete_events / 500.0));
    if(i < lt-1){
        ltm = 0;
        for(int j = 0; j <= i; ++j){
            int amp = (uint16_t)TARGET_AUDIO_BUFFER[j];
            amp = amp >= 32768 ? (amp - 65536) : amp;
            amp = (amp < 0) ? -amp : amp;
            ltm += amp;
        }
        for(int j = AUDIO_SAMPLING_FREQUENCY-1; j > AUDIO_SAMPLING_FREQUENCY-lt+i; --j){
            int amp = (uint16_t)TARGET_AUDIO_BUFFER[j];
            amp = amp >= 32768 ? (amp - 65536) : amp;
            amp = (amp < 0) ? -amp : amp;
            ltm += amp;
        }
        ltm /= lt;
    }
    else{
        ltm = 0;
        for(int j = i-lt+1; j <= i; ++j){
            int amp = (uint16_t)TARGET_AUDIO_BUFFER[j];
            amp = amp >= 32768 ? (amp - 65536) : amp;
            amp = (amp < 0) ? -amp : amp;
            // if(i%16000 == 8000) printf("amp : %ld\n", amp);
            ltm += amp;
        }
        ltm /= lt;
    }
    
    threshold = threshold_const + ltm;

    if(i < st_m-1){
        stm = 0;
        for(int j = 0; j <= i; ++j){
            int amp = (uint16_t)TARGET_AUDIO_BUFFER[j];
            amp = amp >= 32768 ? (amp - 65536) : amp;
            amp = (amp < 0) ? -amp : amp;
            stm = (stm < amp) ? amp : stm;
        }
        for(int j = AUDIO_SAMPLING_FREQUENCY-1; j > AUDIO_SAMPLING_FREQUENCY-st_m+i; --j){
            int amp = (uint16_t)TARGET_AUDIO_BUFFER[j];
            amp = amp >= 32768 ? (amp - 65536) : amp;
            amp = (amp < 0) ? -amp : amp;
            stm = (stm < amp) ? amp : stm;
        }
    }
    else{
        stm = 0;
        for(int j = i-st_m+1; j <= i; ++j){
            int amp = (uint16_t)TARGET_AUDIO_BUFFER[j];
            amp = amp >= 32768 ? (amp - 65536) : amp;
            amp = (amp < 0) ? -amp : amp;
            stm = (stm < amp) ? amp : stm;
        }
    }

    MAX_DATA[MAX_DATA_IX] = stm;
    ++MAX_DATA_IX;
    if(MAX_DATA_IX >= SHORT_TERM_MAX) MAX_DATA_IX = 0;

    uint32_t mean = 0;
    for(int j = 0; j < SHORT_TERM_MAX; ++j){
        mean += MAX_DATA[j];
    }
    mean /= SHORT_TERM_MAX;
    // if(i%1600 == 15) printf("mean : %u\n", this->_time);
    
    if(next_i > 0){
        next_i -= 16;
        return;
    }

    if(mean > threshold){
        // printf("Exceed!!! @ %f s\n", (transfer_complete_events / 500.0));
        max_val = max(max_val, mean - threshold);
        duration += 16;
        exceed_threshold = 1;
    }
    else{
        if(exceed_threshold){
            if(duration <= macd){
                clap_l = max_val;
                next_i = macd;
                if(next_i >= 16000) next_i -= 16000;
                if(clap_l > 0){
                    this->_time = transfer_complete_events;
                    printf("Detected Clap!!! @ %f s , clap_l : %u , threshold : %u , duration : %u\n", (this->_time / 500.0), clap_l, threshold, duration);
                    this->clap_detected();
                }
            }
            else{
                clap_l = 0;
            }
            duration = 0;
            max_val = 0;
        }
        else{
            clap_l = 0;
            duration = 0;
            max_val = 0;
        }
        exceed_threshold = 0;
    }
        
}

int main() {
    printf("BLE init...\n");

    // BLE init
    mbed_trace_init();
    BLE &ble = BLE::Instance();
    ble.onEventsToProcess(schedule_ble_events);
    Clap_Service SVC(ble, event_queue);
    SVC.start();

    ev_queue.dispatch_forever();
}
