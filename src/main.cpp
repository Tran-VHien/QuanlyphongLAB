#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h>

// ================== 1. CẤU HÌNH WIFI & MQTT ==================
const char* ssid = "Mr.UK";
const char* password = "66668888";

const char* mqtt_server = "sd2c6b77.ala.asia-southeast1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_user = "Hien2004";
const char* mqtt_pwd  = "12072004";

// Các Topic MQTT
const char* topic_gui       = "khoavatly/diemdanh";   // Gửi ID vân tay lên Web
const char* topic_nhan      = "khoavatly/dieukhien";  // Nhận lệnh nút bấm
const char* topic_moitruong = "khoavatly/moitruong";  // Gửi Nhiệt độ/Độ ẩm
const char* topic_caidat    = "khoavatly/caidat";     // Nhận giá trị Thanh trượt

WiFiClientSecure espClient;
PubSubClient client(espClient);

// ================== 2. PHẦN CỨNG ==================
#define LED_XANH    4   // Đèn báo hiệu: Sáng khi cửa mở
#define LED_DO      5   // Đèn báo nhiệt độ: Sáng khi nóng (Quạt)
#define BUZZER      19
#define SERVO_PIN   18
#define DHTPIN      23      
#define DHTTYPE     DHT11   

// Cấu hình Serial cho cảm biến vân tay (RX=16, TX=17)
HardwareSerial serialFinger(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialFinger);

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;
DHT dht(DHTPIN, DHTTYPE);

// Biến thời gian cho DHT
unsigned long lastDHTTime = 0;
const long intervalDHT = 3000; 

// Ngưỡng nhiệt độ mặc định
float nguongNhietDo = 35.0; 

// Danh sách sinh viên
const char* ten[11] = {
  "NULL", "TRAN VAN HIEN", "NGUYEN THI LAN", "LE VAN MINH", "PHAM HONG NHUNG",
  "HOANG VAN NAM", "VU THI MAI", "DO VAN QUANG", "BUI THI HUYEN",
  "DINH VAN TUAN", "NGUYEN VAN ANH"
};

// ================== 3. CÁC HÀM HỖ TRỢ ==================

void beepChe(int lan, int tocdo) {
  for (int i = 0; i < lan; i++) {
    digitalWrite(BUZZER, HIGH); delay(tocdo); digitalWrite(BUZZER, LOW); delay(tocdo);
  }
}

void servoGo(int angle) {
  myServo.attach(SERVO_PIN); 
  myServo.write(angle);
  delay(500); 
  myServo.detach(); 
}

// --- HÀM XỬ LÝ MỞ CỬA (ĐÃ SỬA THÊM ĐÈN XANH) ---
void xuLyMoCua() {
  // 1. Bật đèn Xanh báo hiệu được phép vào
  digitalWrite(LED_XANH, HIGH); 
  
  servoGo(100); // Mở cửa
  beepChe(2, 100); 
  
  lcd.clear(); 
  lcd.print(" TU DONG DONG ");
  
  // Đếm ngược 5 giây
  for (int i = 5; i > 0; i--) {
    lcd.setCursor(6, 1); lcd.print(i); lcd.print("s ");
    client.loop(); // Giữ kết nối MQTT
    delay(1000);
  }
  
  servoGo(0); // Đóng cửa
  
  // 2. Tắt đèn Xanh khi cửa đã đóng
  digitalWrite(LED_XANH, LOW); 

  lcd.clear(); 
  lcd.print("HE THONG SAN"); 
  lcd.setCursor(0, 1); 
  lcd.print("DAT VAN TAY");
}

// --- HÀM NHẬN LỆNH TỪ MQTT ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  // 1. Nhận nhiệt độ cài đặt từ Slider
  if (String(topic) == topic_caidat) {
    float tempVal = message.toFloat();
    if (tempVal > 0) { 
        nguongNhietDo = tempVal;
        digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
    }
  }
  // 2. Nhận lệnh điều khiển nút bấm
  else if (String(topic) == topic_nhan) {
    if(message == "OPEN") { 
        lcd.clear(); lcd.print("MO CUA TU XA"); 
        xuLyMoCua(); 
    }
    else if(message == "GREEN_ON") digitalWrite(LED_XANH, HIGH); 
    else if(message == "GREEN_OFF") digitalWrite(LED_XANH, LOW);
    else if(message == "RED_ON") digitalWrite(LED_DO, HIGH); 
    else if(message == "RED_OFF") digitalWrite(LED_DO, LOW); 
    else if(message == "ALARM") { 
        lcd.clear(); lcd.print("!!! BAO DONG !!!"); 
        beepChe(5, 200); 
        lcd.clear(); lcd.print("HE THONG SAN"); lcd.setCursor(0, 1); lcd.print("DAT VAN TAY");
    }
  }
}

void setup_wifi() {
  delay(10);
  lcd.clear(); lcd.print("KET NOI WIFI...");
  WiFi.begin(ssid, password);
  int dem = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); dem++;
    if (dem > 20) { lcd.setCursor(0, 1); lcd.print("WIFI LOI!"); return; }
  }
}

void reconnect() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!client.connected()) {
    String clientId = "ESP32_Admin_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pwd)) {
      client.subscribe(topic_nhan);   
      client.subscribe(topic_caidat); 
    } else {
      delay(2000);
    }
  }
}

// --- LOGIC NHIỆT ĐỘ & ĐÈN ĐỎ (QUẠT) ---
void docVaGuiDHT() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastDHTTime >= intervalDHT) {
    lastDHTTime = currentMillis;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) return;

    String payload = "{\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 0) + "}";
    client.publish(topic_moitruong, payload.c_str());

    // So sánh nhiệt độ với ngưỡng -> Điều khiển đèn đỏ
    if (t > nguongNhietDo) digitalWrite(LED_DO, HIGH); 
    else digitalWrite(LED_DO, LOW);
  }
}

// ================== 4. SETUP & LOOP ==================
void setup() {
  Serial.begin(115200);
  pinMode(LED_XANH, OUTPUT); 
  pinMode(LED_DO, OUTPUT); 
  pinMode(BUZZER, OUTPUT);
  
  dht.begin();
  lcd.init(); lcd.backlight(); 
  myServo.setPeriodHertz(50); servoGo(0); 

  setup_wifi();
  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  serialFinger.begin(57600, SERIAL_8N1, 16, 17);
  finger.begin(57600);
  
  if (!finger.verifyPassword()) {
    lcd.clear(); lcd.print("LOI CAM BIEN"); 
    while (1) delay(1); 
  }
  lcd.clear(); lcd.print("HE THONG SAN"); lcd.setCursor(0, 1); lcd.print("DAT VAN TAY");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnect();
    client.loop();
  }
  
  docVaGuiDHT(); 

  if (finger.getImage() != FINGERPRINT_OK) return; 
  if (finger.image2Tz() != FINGERPRINT_OK) return; 
  
  if (finger.fingerFastSearch() != FINGERPRINT_OK) {
    lcd.clear(); lcd.print("KHONG NHAN DIEN"); 
    beepChe(1, 800); delay(1000);
    lcd.clear(); lcd.print("HE THONG SAN"); lcd.setCursor(0, 1); lcd.print("DAT VAN TAY");
    return;
  }

  // Nếu đúng vân tay:
  uint8_t id = finger.fingerID;
  const char* tenSV = (id >= 1 && id <= 10) ? ten[id] : "UNKNOWN"; 
  
  String payload = "{\"id\":" + String(id) + ",\"name\":\"" + String(tenSV) + "\"}";
  client.publish(topic_gui, payload.c_str());
  
  lcd.clear(); lcd.print("XIN CHAO:"); lcd.setCursor(0, 1); lcd.print(tenSV);
  
  // Gọi hàm mở cửa (sẽ tự bật đèn xanh)
  xuLyMoCua();
}