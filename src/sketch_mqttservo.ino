/*
 *******************************************************************************
 * sketch_mqttservo.ino
 * Prototype for automated window blind control.
 * Standard servo is controlled with MQTT and ESP2866.  Specifically the
 * Sparkfun ESP2866 Thing.  MQTT commands are published via Homebridge, which
 * is running on Raspberry PI.
 * MQTT passes percentages which are mapped to degrees.
 *
 * *
 * Homebridge: https://github.com/nfarina/homebridge
 * homebridge-mqtt-blinds: https://www.npmjs.com/package/homebridge-mqtt-blinds
 ******************************************************************************
 */

#include <string>
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>

/*
 * Servo info
 */
#define SERVO_PIN      (4)                                  // PIN Service data line is attached.
#define MIN_POSITION   (0)                                  // Minimum servo position
#define MAX_POSITION   (170)                                // Maximum servo position, 180 causes server to "chatter"
#define MIN_PERCENTAGE (0)                                  // Minimum percentage from MQTT
#define MAX_PERCENTAGE (100)                                // Maximum percentage from MQTT
#define OPENED         (100)                                // Open position
#define CLOSED         (0)                                  // Close position

Servo servo;                                                // Servo control structure

/*
 * WiFi Settings
 */
const char *ssid     = "<your_ssid>";                       // Name of Wifi Network
const char *password = "<router password>";                 // Password for Wifi Network
WiFiClient wclient;                                         // WiFi client class

/*
 * Sparkfun ESP8266 Thing onboard LED pin
 * Turn on when WiFi connected
 */
#define ESP8266_LED 5

/*
 * Serial port baud rate
 */
#define SERIAL_PORT_BAUD_RATE (115200)

/*
 * MQTT ID and topics
 */
const char *ID             = "Office Blinds";               // Unique name for device.
const char *SET_TOPIC      = "myBlind/SET/targetPosition";  // Topic to subscribe to for servo control

const char *POSITION_TOPIC = "myBlind/GET/currentPosition"; // Topic for servo current position
const char *TARGET_TOPIC   = "myBlind/GET/targetPosition";  // Topic for servo target position
const char *STATE_TOPIC    = "myBlind/GET/positionState";   // Topic for servo state [0:decreasing, 1:increasing, 2:stopped]

/*
 * Enum of motor state
 */
typedef enum state {
  DECREASING,
  INCREASING,
  STOPPED
} State;

/*
 * MQTT Server settings
 */
IPAddress broker(192,168,1,134);                            // IP address of MQTT broker
#define MQTT_BROKER_PORT (1883)                             // MQTT Broker port

PubSubClient client(wclient);                               // Instantiate MQTT client

int currentPosition;                                        // Current motor position


/*
 * Callback to handle incoming messges for the MQTT broker
 */
void mqttcallback(
                  char *topic,        // Inbound topic
                  byte *payload,      // Inbound message
                  unsigned int length // Lenght of payload
                 )
{
  String response;          // Holds response from broker in String format
  int    position;          // Position, in degrees
  int    percentage;        // Integer value of response
  State  currentState;      // Current state of stepper motor
  String strCurrentState;   // String format for currentState
  String strTopic(topic);   // String format topic
  /*
   * Convert payload bytes to String response
   */
  for (unsigned int i = 0; i < length; i++) {
    response += (char)payload[i];
  }

  /*
   * Log debug info
   */
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(response);

  /*
   * Verify topic.  Currently only one topic, SET_TOPIC.
   * If invalid topic received, log it and ignore.
   */
  if (String(SET_TOPIC) != strTopic) {
    Serial.print("Invalid topic received: ");
    Serial.println(strTopic.c_str());
    return;
  }

  percentage = response.toInt();  // Convert response message to integer

  /*
   * map the open/close percentage to degrees
   */
  position = map(percentage, MIN_PERCENTAGE, MAX_PERCENTAGE, MIN_POSITION, MAX_POSITION);

  Serial.print("Position: ");
  Serial.println(position);

  /*
   * Determine motor state
   */
  if (currentPosition > position)
    currentState = DECREASING;
  else if (currentPosition < position)
    currentState = INCREASING;
  else
    currentState = STOPPED;

  strCurrentState = String(currentState);                // Convert int to String
  client.publish(STATE_TOPIC, strCurrentState.c_str());  // Publish motor state

  /*
   *  Move motor to desired position
   */
  servo.write(position);

  /*
   * Publish motor is stopped and position.
   */
  strCurrentState = String(STOPPED);
  client.publish(STATE_TOPIC, strCurrentState.c_str());
  Serial.print("Publish: ");
  Serial.print(STATE_TOPIC);
  Serial.print(":");
  Serial.println(strCurrentState.c_str());

  client.publish(POSITION_TOPIC, response.c_str());
  Serial.print("Publish: ");
  Serial.print(POSITION_TOPIC);
  Serial.print(":");
  Serial.println(response.c_str());
}


/*
 * Reconnect to MQTT broker
 */
void reconnect() {
  /*
   * Loop until we're reconnected
   */
  while (!client.connected()) {
    Serial.print("Establishing MQTT connection...");
    /*
    * Attempt to connect
    */
    if(client.connect(ID)) {
      client.subscribe(SET_TOPIC);
      Serial.println("connected");
      Serial.print("Subscribed to: ");
      Serial.println(SET_TOPIC);
      Serial.println('\n');

    } else {
      Serial.print("Error connecting to MQTT broker. error=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);  // Wait 5 seconds before retrying
    }
  }
}

/*
 * Connect to WiFi network
 */
void setup_wifi() {
  Serial.print("\nConnecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);             // Connect to network

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(500);
    Serial.print(".");
  }

  /*
   * Turn on onboard LED when WiFi is connected.
   */
  digitalWrite(ESP8266_LED, HIGH);

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/*
 * Setup.
 */
void setup() {
 String strPosition; // For converting currentPosition to string
 String strState;    // For converting motor state to string

 Serial.begin(SERIAL_PORT_BAUD_RATE);   // Start serial communication at 115200 baud
 delay(200);                            // Give it some time to get started.
 Serial.println("\nBooting...");

 /*
  * Set onboard LED pin to OUTPUT and turn off.
  * Will be used as WiFi connection indicator.
  */
 pinMode(ESP8266_LED, OUTPUT);
 delay(100);
 digitalWrite(ESP8266_LED, LOW);

 /*
  * Attach to servo PIN
  */
 servo.attach(SERVO_PIN);

 /*
  * Set to motor position to "open"
  */
 servo.write(OPENED);

 /*
  * Update current position.
  */
 currentPosition = OPENED;
 strPosition = String(currentPosition);  // Convert to string

 /*
  * Connect to WiFi
  */
 setup_wifi();


 /*
  * Setup MQTT broker, port, and callback.
  */
 client.setServer(broker, MQTT_BROKER_PORT);
 client.setCallback(mqttcallback);

 /*
  * Post motor position
  */
 client.publish(POSITION_TOPIC, strPosition.c_str());

 /*
  * Post motor state.
  */
 strState = String(STOPPED);
 client.publish(STATE_TOPIC, strState.c_str());

}

/*
 * Main process loop.
 */
void loop() {

  /*
   * Check WiFi connection and reconnect if necessary.
   */
  if (WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_CONNECTION_LOST) {
    /*
     * Turn off onboard LED when WiFi is connected.
     */
    digitalWrite(ESP8266_LED, LOW);

    /*
     * Call setup_wifi() to try to reconnect.
     */
    setup_wifi();

  }

  /*
   * Check connection to MQTT broker.
   */
  if (!client.connected())  // Reconnect if connection is lost
  {
    reconnect();
  }

  /*
   * MQTT processing loop
   */
  client.loop();
}
