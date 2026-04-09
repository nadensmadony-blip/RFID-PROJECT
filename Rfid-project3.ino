#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <SPIFFS.h>
#include <ThreeWire.h>  
#include <RtcDS1302.h>

// --- إعدادات الساعة ---
ThreeWire myWire(17, 16, 5); 
RtcDS1302<ThreeWire> Rtc(myWire);

// --- إعدادات الـ RFID والـ Buzzer ---
#define RFID_SS 2
#define RFID_RST 4
const int buzzerPin = 15; // تم ضبطه على دبوس 14 كما طلبت
MFRC522 mfrc522(RFID_SS, RFID_RST);

// --- إعدادات الـ SD Card ---
#define SD_SCK  14
#define SD_MISO 12
#define SD_MOSI 13
#define SD_CS   27
SPIClass myHSPI(HSPI); 

LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

const char* adminPass = "1212";

// --- دالة نغمة الجرس (Beep) ---
void triggerBeep() {
  digitalWrite(buzzerPin, LOW);  // تشغيل (Active Low)
  delay(150);  
  digitalWrite(buzzerPin, HIGH); // إيقاف
}

// دالة جلب الوقت
String getNowTime() {
    RtcDateTime now = Rtc.GetDateTime();
    char timeBuf[20];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u", now.Hour(), now.Minute(), now.Second());
    return String(timeBuf);
}

// دالة فصل البيانات
String getValue(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;
    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// --- Web Server Handlers ---

void handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center; background:#f4f4f4;} .box{background:white; padding:20px; border-radius:10px; display:inline-block; margin-top:50px; box-shadow:0 0 10px rgba(0,0,0,0.1);} input{padding:10px; margin:10px; width:80%;}</style></head><body>";
    html += "<div class='box'><h2>Student Registration</h2><form action='/submit' method='POST'>";
    html += "<input type='text' name='name' placeholder='Student Name' required><br>";
    html += "<input type='text' name='seat' placeholder='Seat Number' required><br>";
    html += "<input type='submit' value='Register' style='background:#4CAF50; color:white; border:none; padding:10px 20px;'></form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleManualSubmit() {
    String name = server.arg("name");
    String seat = server.arg("seat");
    File log = SPIFFS.open("/attendance.csv", FILE_APPEND);
    if (log) {
        log.println(name + "," + seat + ",Manual_Entry," + getNowTime());
        log.close();
        triggerBeep(); // تفعيل الجرس عند التسجيل اليدوي
    }
    server.send(200, "text/html", "<body style='text-align:center;'><h3>Done!</h3><a href='/'>Back</a></body>");
}

void handleAdmin() {
    if (!server.hasArg("pass") || server.arg("pass") != adminPass) {
        String loginHtml = "<html><body style='text-align:center;'><h2>Admin Login</h2><form action='/admin'><input type='password' name='pass'><input type='submit' value='Login'></form></body></html>";
        server.send(200, "text/html", loginHtml);
        return;
    }
    File file = SPIFFS.open("/attendance.csv", FILE_READ);
    String html = "<html><head><meta charset='UTF-8'><style>table{width:100%; border-collapse:collapse;} th,td{border:1px solid #ddd; padding:8px; text-align:center;} th{background:#333; color:white;}</style></head><body>";
    html += "<h2>Attendance Records</h2><table><tr><th>#</th><th>Name</th><th>Seat</th><th>ID/Type</th><th>Time</th><th>Action</th></tr>";
    int i = 1;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 2) {
            html += "<tr><td>"+String(i++)+"</td><td>"+getValue(line,',',0)+"</td><td>"+getValue(line,',',1)+"</td><td>"+getValue(line,',',2)+"</td><td>"+getValue(line,',',3)+"</td><td><a href='/delete?line="+String(i-2)+"&pass=1212'>Delete</a></td></tr>";
        }
    }
    file.close();
    html += "</table><br><a href='/clearAll?pass=1212' style='color:red;'>Clear All</a></body></html>";
    server.send(200, "text/html", html);
}

void handleDelete() {
    if (server.arg("pass") != adminPass) { server.send(403, "text/plain", "Unauthorized"); return; }
    int lineToDelete = server.arg("line").toInt();
    File file = SPIFFS.open("/attendance.csv", FILE_READ);
    File temp = SPIFFS.open("/temp.csv", FILE_WRITE);
    int currentLine = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (currentLine != lineToDelete) temp.println(line);
        currentLine++;
    }
    file.close(); temp.close();
    SPIFFS.remove("/attendance.csv");
    SPIFFS.rename("/temp.csv", "/attendance.csv");
    server.sendHeader("Location", "/admin?pass=1212");
    server.send(303);
}

void setup() {
    // إعدادات الجرس لمنع الصوت المستمر عند التشغيل
    pinMode(buzzerPin, INPUT_PULLUP); 
    Serial.begin(115200);
    Wire.begin(21, 22);
    lcd.init(); lcd.backlight();
    
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, HIGH); // إغلاق الجرس (Off)

    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    Rtc.SetDateTime(compiled); 

    WiFi.softAP("Tanta_System", "12345678");
    SPIFFS.begin(true);
    SPI.begin();
    mfrc522.PCD_Init();
    myHSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    SD.begin(SD_CS, myHSPI);

    server.on("/", handleRoot);
    server.on("/submit", HTTP_POST, handleManualSubmit);
    server.on("/admin", handleAdmin);
    server.on("/delete", handleDelete);
    server.on("/clearAll", []() {
        if(server.arg("pass") == adminPass) { SPIFFS.remove("/attendance.csv"); server.sendHeader("Location", "/admin?pass=1212"); server.send(303); }
    });

    server.begin();
    lcd.print("System Ready");
}

void loop() {
    server.handleClient();

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
            uid += String(mfrc522.uid.uidByte[i], HEX);
        }
        uid.toUpperCase();

        String studentName = "Unknown";
        String seatNumber = "0000";
        
        File file = SD.open("/students.txt");
        if (file) {
            while (file.available()) {
                String line = file.readStringUntil('\n');
                if (line.indexOf(uid) != -1) {
                    studentName = getValue(line, ',', 0);
                    seatNumber = getValue(line, ',', 2);
                    break;
                }
            }
            file.close();
        }

        File log = SPIFFS.open("/attendance.csv", FILE_APPEND);
        if (log) {
            log.println(studentName + "," + seatNumber + "," + uid + "," + getNowTime());
            log.close();
            triggerBeep(); // تفعيل الجرس عند قراءة الكارت بنجاح
        }

        lcd.clear();
        lcd.print("NAME: " + studentName);
        lcd.setCursor(0, 1);
        lcd.print("ID: " + uid);

        delay(3000);
        lcd.clear(); lcd.print("Scan Your Card");
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
    }
}