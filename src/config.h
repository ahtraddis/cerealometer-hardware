#ifndef CONFIG_H
#define CONFIG_H

// Replace with your actual SSIDs and passwords
#define WIFI_SSID "<Your-SSID-goes-here>"
#define WIFI_PASSWORD "<Your-password-goes-here>"
#define WIFI_SSID2 "<Your-alternate-SSID-goes-here>"
#define WIFI_PASSWORD2 "<Your-alternate-password-goes-here>"

// Firebase hostname without http scheme, e.g. my-project-0123456789.firebaseio.com
#define FIREBASE_HOST "<Your-hostname-goes-here>"
#define FIREBASE_AUTH "<Your-database-secret-goes-here>"
// Firebase REST API endpoint URL used to post weight updates
#define REST_API_ENDPOINT "Your-URL-goes-here>"
#define DEVICE_ID "<Your-device-id-goes-here>" // temp hardcoded ID for Firebase

#endif
