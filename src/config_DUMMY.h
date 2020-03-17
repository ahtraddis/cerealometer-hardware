#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_AP_SSID "CerealometerAP"
#define WIFI_AP_PASSWORD "LuckyCh@rms!"

// Firebase hostname without http scheme, e.g. my-project-0123456789.firebaseio.com
#define FIREBASE_HOST_PATTERN "%%FIREBASE_PROJECT_ID%%.firebaseio.com"
// Firebase REST API endpoint URL, e.g. http://us-central1-my-project-0123456789.cloudfunctions.net
#define REST_API_BASEURL_PATTERN "http://us-central1-%%FIREBASE_PROJECT_ID%%.cloudfunctions.net"

#endif
