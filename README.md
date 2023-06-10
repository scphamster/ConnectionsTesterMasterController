<h1> Connections tester </h1>
<h2> Technologies stack </h2>
<ul>
  <li> microcontroller: ESP32 </li>
  <li> Main Framework: ESP-IDF:V4.4 </li>
  <li> Bluetooth classic </li>
  <li> WiFi </li>
  <li> TCP Socket </li>
  <li> ASIO </li>
  <li> I2C </li>
</ul>

<h1> Description </h1>
This application is part of a project composed of three repositories:
<ul>
  <li> <a href=https://github.com/scphamster/ConnectionsTesterMasterController/tree/master>This one - master controller for IO boards group</a> </li>
  <li> <a href=https://github.com/scphamster/ConnectionsTesterBoardController>Controller for IO board (based on ATTiny84a)</a></li>
  <li> <a href=https://github.com/scphamster/ConnectionsTesterAndroidApp>Android App for control of the device in general</a></li>
</ul>

<h2>Theory of operation</h2>
Device listens for commands from Android app, two of those commands may be considered as fundamental, these are: "Check connectivity for pin X at board with id Y" and "Check all connections". The other commands are related to setup of device, firmware version check and other commands required for work. The device is connected via i2c bus to many IO boards which are in turn listening to it for commands such as "Set voltage output at pin X" and "Read all voltages", the measurement of connectivity is mainly done leveraging these two.

  
