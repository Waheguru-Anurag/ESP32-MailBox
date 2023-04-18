#include "WiFi.h"
#include "string.h"
#include <FS.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "SPI.h"
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include "driver/rtc_io.h"

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

#define TIME_FREQUENCY_MINUTES 5
#define FILE_PHOTO "/photo.jpg"

#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#else
  #error "Camera model not selected"
#endif

/*. Firebase Credentials */
#define API_KEY "..."
#define USER_EMAIL "..."
#define USER_PASSWORD "..."
#define STORAGE_BUCKET_ID "..."

/* Wifi Credentials */
const char* ssid = "...";
const char* password = "...";


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;
int pictureCount;

void initWifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully");
  }
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 

  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

void initFirebase() {
  configF.api_key = API_KEY;
    //Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  //Assign the callback function for the long running token generation task
  configF.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);
}

bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

void takePhoto() {
  camera_fb_t * fb = NULL;

  bool ok = 0;
  
  do {
    // Take Picture with Camera
    fb = esp_camera_fb_get();  
    if(!fb) {
      Serial.println("Camera capture failed");
      return;
    }

    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);

    // Check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
  } while ( !ok );
}

void sendPhoto() {
  if (Firebase.ready()){
    Serial.print("Uploading picture... ");

    String FILE_REMOTE_PATH = "/photo";
    FILE_REMOTE_PATH = FILE_REMOTE_PATH + pictureCount + ".jpg";

    Serial.println(FILE_REMOTE_PATH);
    
    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, FILE_PHOTO, mem_storage_type_flash, FILE_REMOTE_PATH, "image/jpg")){
      Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
      pictureCount += 1;
    }
    else{
      Serial.println(fbdo.errorReason());
    }
  }
}

int getFirestoreImageCount() {
  Firebase.Storage.listFiles(&fbdo, STORAGE_BUCKET_ID);

  if (fbdo.httpCode() == FIREBASE_ERROR_HTTP_CODE_OK)
  {
      FileList *files = fbdo.fileList();
      int size = files->items.size();
      fbdo.fileList()->items.clear();
      return size;
  }
  
  return 0;
}

void setup()
{
    Serial.begin(115200);

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Initialize Wifi
    initWifi();

    // Initialize Storage
    initSPIFFS();

    // Initialize Camera
    initCamera();
     
    // Initialize Firebase
    initFirebase();

    // Get Existing Picture Count in Firebase
    pictureCount = getFirestoreImageCount();
    
    Serial.print("Existing Picture Count: "); 
    Serial.println(pictureCount);
}

void loop()
{ 
    // Take and Save Photo
    takePhoto();
    delay(1000 * 30 * TIME_FREQUENCY_MINUTES);

    // Send Photo
    sendPhoto();
    delay(1000 * 30 * TIME_FREQUENCY_MINUTES);
}
