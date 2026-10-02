#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <NRFLite.h>
#include <FastLED.h>

#define E_STOP 14

// WS2812B status
constexpr int STAT_LED = 48;
CRGB led_stat[1];

// WS2812B string
constexpr int LED_PIN = 17;
constexpr int NUM_LEDS = 20;
constexpr uint8_t LED_BRIGHTNESS = 64;   // 0-255, 25% is plenty indoors
CRGB leds[NUM_LEDS];

// // Shared SPI bus (SPI2/FSPI native pins)
// constexpr int SPI_SCK = 12, SPI_MOSI = 11, SPI_MISO = 13;
// // Chip selects and control pins
// constexpr int VFD_CS = 10, VFD_RST = 14;
// VFD: own bus (SPI3/HSPI)
constexpr int VFD_SCK = 12, VFD_MOSI = 11, VFD_CS = 10, VFD_RST = 13;
// nRF24: global SPI (SPI2/FSPI), required by NRFLite
constexpr int NRF_SCK = 40, NRF_MOSI = 41, NRF_MISO = 42, NRF_CSN = 15, NRF_CE = 16;
// Rotary encoder (moved off 40-42)
constexpr int ENC_A = 39, ENC_B = 38, ENC_SW = 47;

// NRF24L01
const static uint8_t RADIO_ID = 112;
const static uint8_t DESTINATION_RADIO_ID = 97;

struct __attribute__((packed)) RadioPacket {
  uint8_t brightKey;
  uint16_t zone;
  uint16_t qcomm;
  uint16_t color;
  uint16_t speed;
  uint16_t extra;
  uint16_t rh;
  uint16_t gs;
  uint16_t bv;
  uint8_t rh2;
  uint8_t gs2;
  uint8_t bv2;
  uint8_t ext1;
  uint8_t ext2;
  uint8_t ext3;
  uint8_t ext4;
  uint8_t sw1;
  uint8_t sw2;
  uint16_t randfactor;
  uint8_t formfactor;
  uint8_t cclass;
  uint8_t rev;
};

NRFLite _base;
RadioPacket _satelliteData;
unsigned long lastPacketSent;

void sendPacket() {
  if(millis() - lastPacketSent > 10) {
    if(!_base.send(DESTINATION_RADIO_ID, &_satelliteData, sizeof(_satelliteData), NRFLite::NO_ACK)) {
      // leds[0] = CRGB::Red;
      // FastLED.show();
      delay(500);
      Serial.println("send packet failed");
    } else {

    }
    lastPacketSent = millis();
  }
}

// Touch slider
const uint8_t pads[] = {1, 2, 3, 4, 5, 6};
constexpr int N_PADS = sizeof(pads);
uint32_t baseline[N_PADS];

SPIClass vfdSPI(HSPI);

uint8_t vfd_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  switch (msg) {
    case U8X8_MSG_BYTE_SEND:
      vfdSPI.writeBytes((uint8_t *)arg_ptr, arg_int);
      break;
    case U8X8_MSG_BYTE_INIT:
      if (u8x8->bus_clock == 0) u8x8->bus_clock = u8x8->display_info->sck_clock_hz;
      u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
      break;
    case U8X8_MSG_BYTE_SET_DC:
      u8x8_gpio_SetDC(u8x8, arg_int);
      break;
    case U8X8_MSG_BYTE_START_TRANSFER: {
      static const uint8_t modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
      vfdSPI.beginTransaction(SPISettings(u8x8->bus_clock, MSBFIRST,
                                          modes[u8x8->display_info->spi_mode & 3]));
      u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
      u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO,
                              u8x8->display_info->post_chip_enable_wait_ns, NULL);
      break;
    }
    case U8X8_MSG_BYTE_END_TRANSFER:
      u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO,
                              u8x8->display_info->pre_chip_disable_wait_ns, NULL);
      u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
      vfdSPI.endTransaction();
      break;
    default:
      return 0;
  }
  return 1;
}

U8G2_GP1294AI_256X48_F_4W_HW_SPI u8g2(U8G2_R0, VFD_CS, U8X8_PIN_NONE, VFD_RST);

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(E_STOP, INPUT_PULLUP);

  // FastLED.addLeds<WS2812, STAT_LED, GRB>(led_stat, 1);
  // FastLED.setBrightness(30);
  // delay(100);
  // fill_solid(led_stat, 1 , CRGB::Red);
  // FastLED.show();
  // delay(1000);

  // FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  // FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);   // cap current draw
  // FastLED.clear(true);                              // all off, pushed immediately
  // Serial.println("LEDs initialized");
  // led_stat[0] = CRGB::Green;
  
  // delay(500);
  // fill_solid(leds, NUM_LEDS, CRGB::Green);
  // FastLED.show();

  // VFD on its own bus
  vfdSPI.begin(VFD_SCK, -1, VFD_MOSI, -1);      // no MISO; CS driven by U8g2
  u8g2.getU8x8()->byte_cb = vfd_byte_cb;        // must be set before begin()
  // u8g2.begin();
  // u8g2.setContrast(255);

  // nRF on global SPI
  pinMode(NRF_CSN, OUTPUT); digitalWrite(NRF_CSN, HIGH);
  SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI, -1);
  if (!_base.init(RADIO_ID, NRF_CE, NRF_CSN, NRFLite::BITRATE2MBPS, 100, 0)) {
    Serial.println("nRF24L01 not responding, check wiring/power");
    while (1) {
      // led_stat[0] = CRGB::Red;
      FastLED.show();
      delay(500);
      // led_stat[0] = CRGB::Black;
      FastLED.show();
      delay(500);
    }
  }
  Serial.println("nRF24L01 initialized");
  lastPacketSent = millis();
  // led_stat[0] = CRGB::White;
  // FastLED.show();
  delay(2000);

  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  u8g2.begin();                                  // SPI.begin() inside is a no-op now
  u8g2.setContrast(255);
  Serial.println("VFD initialized");
  // led_stat[0] = CRGB::Blue;
  // FastLED.show();
  delay(500);

  

  // for (int i = 0; i < N_PADS; i++) {             // don't touch pads during boot
  //   uint64_t sum = 0;
  //   for (int s = 0; s < 32; s++) { sum += touchRead(pads[i]); delay(5); }
  //   baseline[i] = sum / 32;
  // }
  // Serial.println("Touch slider calibrated");

  
}

void drawLogo(void)
{
    u8g2.setFontMode(1);	// Transparent
    u8g2.setFontDirection(0);
    u8g2.setFont(u8g2_font_inb24_mf);
    u8g2.drawStr(0, 30, "U");
    
    u8g2.setFontDirection(1);
    u8g2.setFont(u8g2_font_inb30_mn);
    u8g2.drawStr(21,8,"8");
        
    u8g2.setFontDirection(0);
    u8g2.setFont(u8g2_font_inb24_mf);
    u8g2.drawStr(51,30,"g");
    u8g2.drawStr(67,30,"\xb2");
    
    u8g2.drawHLine(2, 35, 47);
    u8g2.drawHLine(3, 36, 47);
    u8g2.drawVLine(45, 32, 12);
    u8g2.drawVLine(46, 33, 12);
}

void drawURL(void)
{
  u8g2.setFont(u8g2_font_4x6_tr);
  if ( u8g2.getDisplayHeight() < 59 )
  {
    u8g2.drawStr(89,20,"github.com");
    u8g2.drawStr(73,29,"/olikraus/u8g2");
  }
}

void loop() {
  u8g2.clearBuffer();
  drawLogo();
  drawURL();
  u8g2.sendBuffer();
  delay(1000);


  _satelliteData.qcomm = 0;
  _satelliteData.extra = 0;
  _satelliteData.speed = 0;
  _satelliteData.randfactor = 0;

  _satelliteData.formfactor = 0;
  _satelliteData.cclass = 0;
  //software revision / pattern support
  _satelliteData.rev = 0;
  _satelliteData.brightKey = 255;
  _satelliteData.zone = 0;

  sendPacket();
  Serial.println("sent radio command");
  delay(5000);
}