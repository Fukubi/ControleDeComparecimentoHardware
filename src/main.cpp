#include <HTTPClient.h>
#include <MFRC522.h>
#include <SPI.h>
#include <WiFi.h>
#include <string>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const int RST_PIN = 22; // Reset pin
const int SS_PIN = 21;  // Slave select pin

// Size RFID Buffer
#define SIZE_BUFFER 18
// Max size of RFID memory block
#define MAX_SIZE_BLOCK 16

// WiFi Settings
#define SSID "Projeto"
#define WIFI_PASS "1234567890"

// Firebase base URL
#define FIREBASE_SERVER                                                        \
  "https://projetointroducaoec-default-rtdb.firebaseio.com/pontos/"

// NTP Server settings
#define NTP_SERVER "time.google.com"
#define GMTOFFSET_SEC -14400
#define DAYLIGHTOFFSET_SEC 3600

// GPIO Pins
#define YELLOW_LED 25
#define GREEN_LED 26
#define RED_LED 23

MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

MFRC522 mfrc522(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(9600);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(YELLOW_LED, LOW);

  while (!Serial)
    ;

  // Initialize SPI
  SPI.begin();
  // Initialize MFRC522
  mfrc522.PCD_Init();
  // Show details of MFRC522 for Debug
  mfrc522.PCD_DumpVersionToSerial();

  // Settings the WiFi to Station
  WiFi.mode(WIFI_STA);
  // Connecting to wifi
  WiFi.begin(SSID, WIFI_PASS);

  Serial.println("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  // Print basic WiFi data for Debug purposes
  Serial.println(WiFi.localIP());
  Serial.print("RRSI: ");
  Serial.println(WiFi.RSSI());

  // Settings of NTP server
  configTime(GMTOFFSET_SEC, DAYLIGHTOFFSET_SEC, NTP_SERVER);

  // Initialization complete
  digitalWrite(YELLOW_LED, HIGH);
  Serial.println(F("Scan PICC to see UID, SAK, type, and data blocks..."));
}

// faz a gravação dos dados no cartão/tag
void writeData() {
  // imprime os detalhes tecnicos do cartão/tag
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));
  // aguarda 30 segundos para entrada de dados via Serial
  Serial.setTimeout(30000L);
  Serial.println(F("Insira os dados a serem gravados com o caractere '#' ao "
                   "final\n[máximo de 16 caracteres]:"));

  // Prepara a chave - todas as chaves estão configuradas para FFFFFFFFFFFFh
  // (Padrão de fábrica).
  for (byte i = 0; i < 6; i++)
    key.keyByte[i] = 0xFF;

  // buffer para armazenamento dos dados que iremos gravar
  byte buffer[MAX_SIZE_BLOCK] = "";
  byte bloco;        // bloco que desejamos realizar a operação
  byte tamanhoDados; // tamanho dos dados que vamos operar (em bytes)

  // recupera no buffer os dados que o usuário inserir pela serial
  // serão todos os dados anteriores ao caractere '#'
  tamanhoDados = Serial.readBytesUntil('#', (char *)buffer, MAX_SIZE_BLOCK);
  // espaços que sobrarem do buffer são preenchidos com espaço em branco
  for (byte i = tamanhoDados; i < MAX_SIZE_BLOCK; i++) {
    buffer[i] = ' ';
  }

  bloco = 1;                   // bloco definido para operação
  String str = (char *)buffer; // transforma os dados em string para imprimir
  Serial.println(str);

  // Authenticate é um comando para autenticação para habilitar uma comuinicação
  // segura
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloco,
                                    &key, &(mfrc522.uid));

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    delay(1000);
    return;
  }
  // else Serial.println(F("PCD_Authenticate() success: "));

  // Grava no bloco
  status = mfrc522.MIFARE_Write(bloco, buffer, MAX_SIZE_BLOCK);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Write() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    delay(1000);
    return;
  } else {
    Serial.println(F("MIFARE_Write() success: "));
    delay(1000);
  }
}

void postToFirebase(const char *firebaseURL, const char *requestBody) {
  HTTPClient http;

  http.begin(firebaseURL);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.PUT(requestBody);

  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode == 200) {
    digitalWrite(GREEN_LED, HIGH);
    delay(1000);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(RED_LED, HIGH);
    delay(1000);
    digitalWrite(RED_LED, LOW);
  }

  http.end();
}

int getFormattedCurrentDateAndTime(char *bufferDate, char *bufferTime) {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return 1;
  }

  strftime(bufferDate, 10, "%Y%m%d", &timeinfo);
  strftime(bufferTime, 10, "%T", &timeinfo);

  return 0;
}

int readRFIDData(char *dataBuffer) {
  // imprime os detalhes tecnicos do cartão/tag
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));
  char data[MAX_SIZE_BLOCK];

  // Prepara a chave - todas as chaves estão configuradas para FFFFFFFFFFFFh
  // (Padrão de fábrica).
  for (byte i = 0; i < 6; i++)
    key.keyByte[i] = 0xFF;

  // buffer para colocar os dados ligos
  byte buffer[SIZE_BUFFER] = {0};

  // bloco que faremos a operação
  byte bloco = 1;
  byte tamanho = SIZE_BUFFER;

  // faz a autenticação do bloco que vamos operar
  status =
      mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloco, &key,
                               &(mfrc522.uid)); // line 834 of MFRC522.cpp file
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 1;
  }

  // faz a leitura dos dados do bloco
  status = mfrc522.MIFARE_Read(bloco, buffer, &tamanho);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Reading failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 1;
  }

  Serial.print(F("\nDados bloco ["));
  Serial.print(bloco);
  Serial.print(F("]: "));

  for (uint8_t i = 0; i < MAX_SIZE_BLOCK; i++) {
    if (buffer[i] == ' ') {
      data[i] = '\0';
    } else {
      data[i] = (char)buffer[i];
    }
  }

  strcpy(dataBuffer, data);
  return 0;
}

void sendRegisterToFirebase() {
  char firebaseServerFullURL[100];
  char data[MAX_SIZE_BLOCK];
  char requestBody[200];
  char todayDate[10];
  char nowTime[10];

  if (getFormattedCurrentDateAndTime(todayDate, nowTime)) {
    digitalWrite(RED_LED, HIGH);
    delay(1000);
    digitalWrite(RED_LED, LOW);
  }

  Serial.println(todayDate);
  Serial.println(nowTime);

  if (readRFIDData(data)) {
    digitalWrite(RED_LED, HIGH);
    delay(1000);
    digitalWrite(RED_LED, LOW);
    return;
  };

  Serial.println(data);

  strcpy(firebaseServerFullURL, FIREBASE_SERVER);
  strcat(firebaseServerFullURL, todayDate);
  strcat(firebaseServerFullURL, "/");
  strcat(firebaseServerFullURL, data);
  strcat(firebaseServerFullURL, ".json");

  strcpy(requestBody, "{\"datetime\": \"");
  strcat(requestBody, nowTime);
  strcat(requestBody, "\"}");

  Serial.println(firebaseServerFullURL);
  Serial.println(requestBody);

  postToFirebase(firebaseServerFullURL, requestBody);
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent())
    return;

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  sendRegisterToFirebase();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}