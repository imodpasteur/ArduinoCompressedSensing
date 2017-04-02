///////////////////////////////////////////////////////////////////////////////
// FILE:          Arduino.cpp
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   Arduino adapter.  Needs accompanying firmware
// COPYRIGHT:     University of California, San Francisco, 2008
// LICENSE:       LGPL
// 
// AUTHOR:        Nico Stuurman, nico@cmp.ucsf.edu 11/09/2008
//                automatic device detection by Karl Hoover
//
//

#include "Arduino.h"
#include "../../MMDevice/ModuleInterface.h"
#include <sstream>
#include <cstdio>
#include <string>
#include <iostream>

#ifdef WIN32
   #define WIN32_LEAN_AND_MEAN
   #include <windows.h>
   #define snprintf _snprintf 
#endif

const char* g_DeviceNameArduinoHub = "Arduino-Hub";
const char* g_DeviceNameArduinoSwitch = "Arduino-Switch";
const char* g_DeviceNameArduinoShutter = "Arduino-Shutter";
const char* g_DeviceNameArduinoDA1 = "Arduino-DAC1";
const char* g_DeviceNameArduinoDA2 = "Arduino-DAC2";
const char* g_DeviceNameArduinoInput = "Arduino-Input";

const char* g_DeviceNameDAZStage = "DA Z Stage";
const char* g_PropertyMinUm = "Stage Low Position(um)";
const char* g_PropertyMaxUm = "Stage High Position(um)";


// Global info about the state of the Arduino.  This should be folded into a class
const int g_Min_MMVersion = 1;
const int g_Max_MMVersion = 3; // CS: changed from 2
const int cs_version_allowed_ = 3; // CS: created
const char* g_versionProp = "Version";
const char* g_normalLogicString = "Normal";
const char* g_invertedLogicString = "Inverted";

const char* g_On = "On";
const char* g_Off = "Off";

// static lock
MMThreadLock CArduinoHub::lock_;

///////////////////////////////////////////////////////////////////////////////
// Exported MMDevice API
///////////////////////////////////////////////////////////////////////////////
MODULE_API void InitializeModuleData()
{
   RegisterDevice(g_DeviceNameArduinoHub, MM::HubDevice, "Hub (required)");
   RegisterDevice(g_DeviceNameArduinoSwitch, MM::StateDevice, "Digital out 8-bit");
   RegisterDevice(g_DeviceNameArduinoShutter, MM::ShutterDevice, "Shutter");
   RegisterDevice(g_DeviceNameArduinoDA1, MM::SignalIODevice, "DAC channel 1");
   RegisterDevice(g_DeviceNameArduinoDA2, MM::SignalIODevice, "DAC channel 2");
   RegisterDevice(g_DeviceNameArduinoInput, MM::GenericDevice, "ADC");
   
   //RegisterDevice(g_DeviceNameDAZStage, MM::StageDevice, "Arduino-controlled Z-stage"); // Added to control Stage
}

MODULE_API MM::Device* CreateDevice(const char* deviceName)
{
   if (deviceName == 0)
      return 0;

   if (strcmp(deviceName, g_DeviceNameArduinoHub) == 0)
   {
      return new CArduinoHub;
   }
   else if (strcmp(deviceName, g_DeviceNameArduinoSwitch) == 0)
   {
      return new CArduinoSwitch;
   }
   else if (strcmp(deviceName, g_DeviceNameArduinoShutter) == 0)
   {
      return new CArduinoShutter;
   }
   else if (strcmp(deviceName, g_DeviceNameArduinoDA1) == 0)
   {
      return new CArduinoDA(1); // channel 1
   }
   else if (strcmp(deviceName, g_DeviceNameArduinoDA2) == 0)
   {
      return new CArduinoDA(2); // channel 2
   }
   else if (strcmp(deviceName, g_DeviceNameArduinoInput) == 0)
   {
      return new CArduinoInput;
   }
   //lse if (strcmp(deviceName, g_DeviceNameDAZStage) == 0)
   //{
   //   return new CArduinoZStage;
   //}   

   return 0;
}

MODULE_API void DeleteDevice(MM::Device* pDevice)
{
   delete pDevice;
}

///////////////////////////////////////////////////////////////////////////////
// CArduinoHUb implementation
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
CArduinoHub::CArduinoHub() :
   initialized_ (false),
   switchState_ (0),
   shutterState_ (0)
{
   portAvailable_ = false;
   invertedLogic_ = false;
   timedOutputActive_ = false;

   InitializeDefaultErrorMessages();

   SetErrorText(ERR_PORT_OPEN_FAILED, "Failed opening Arduino USB device");
   SetErrorText(ERR_BOARD_NOT_FOUND, "Did not find an Arduino board with the correct firmware.  Is the Arduino board connected to this serial port?");
   SetErrorText(ERR_NO_PORT_SET, "Hub Device not found.  The Arduino Hub device is needed to create this device");
   std::ostringstream errorText;
   errorText << "The firmware version on the Arduino is not compatible with this adapter.  Please use firmware version ";
   errorText <<  g_Min_MMVersion << " to " << g_Max_MMVersion;
   SetErrorText(ERR_VERSION_MISMATCH, errorText.str().c_str());

   CPropertyAction* pAct = new CPropertyAction(this, &CArduinoHub::OnPort);
   CreateProperty(MM::g_Keyword_Port, "Undefined", MM::String, false, pAct, true);

   pAct = new CPropertyAction(this, &CArduinoHub::OnLogic);
   CreateProperty("Logic", g_invertedLogicString, MM::String, false, pAct, true);

   AddAllowedValue("Logic", g_invertedLogicString);
   AddAllowedValue("Logic", g_normalLogicString);  
}

CArduinoHub::~CArduinoHub()
{
   Shutdown();
}

void CArduinoHub::GetName(char* name) const
{
   CDeviceUtils::CopyLimitedString(name, g_DeviceNameArduinoHub);
}

bool CArduinoHub::Busy()
{
   return false;
}

// * Return the version of the Arduino firmware *
// private and expects caller to:
// 1. guard the port
// 2. purge the port
int CArduinoHub::GetControllerVersion(int& version)
{
   int ret = DEVICE_OK;
   unsigned char command[1];
   
   // Check if the Arduino has a MM-compatible firmware loaded
   command[0] = 30;
   version = 0;

   ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 1);
   if (ret != DEVICE_OK)
      return ret;

   std::string answer;
   ret = GetSerialAnswer(port_.c_str(), "\r\n", answer);
   if (ret != DEVICE_OK)
      return ret;

   if (answer != "MM-Ard")
      return ERR_BOARD_NOT_FOUND;

   // Check version number of the Arduino
   command[0] = 31;
   ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 1);
   if (ret != DEVICE_OK)
      return ret;

   std::string ans;
   ret = GetSerialAnswer(port_.c_str(), "\r\n", ans);
   if (ret != DEVICE_OK) {
         return ret;
   }
   std::istringstream is(ans);
   is >> version;

   return ret;

}

// Compressed Sensing
// Check if the firmware has compressed sensing capabilities
// Assumes that the version already matches
int CArduinoHub::GetCSMode(int& yesno)
{
   int ret = DEVICE_OK;
   unsigned char command[1];
   command[0] = 33;
   yesno = 0;

   ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 1);
   if (ret != DEVICE_OK)
      return ret;

    std::string ans;
    ret = GetSerialAnswer(port_.c_str(), "\r\n", ans);
    if (ret != DEVICE_OK) {
         return ret;
    }

    std::string answer;
    std::istringstream is(ans);
    is >> answer;
    
   if (answer == "CS_enabled") {
       yesno = 1;
   } else {
       yesno = 0;
   }
   return ret;
}

// Compressed sensing
// Get the identifier of the basis
// This is used as a short way of checking whether the loaded basis is the 
// right one and avoid to download the whole basis (although this is possible)
int CArduinoHub::GetCSBasisId(int& basis_id)
{
   int ret = DEVICE_OK;
   unsigned char command[1];
   basis_id = 0;

   // Check version number of the Arduino
   command[0] = 34;
   ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 1);
   if (ret != DEVICE_OK)
      return ret;

   std::string ans;
   ret = GetSerialAnswer(port_.c_str(), "\r\n", ans);
   if (ret != DEVICE_OK) {
         return ret;
   }
   std::istringstream is(ans);
   is >> basis_id;

   return ret;

}

bool CArduinoHub::SupportsDeviceDetection(void)
{
   return true;
}

MM::DeviceDetectionStatus CArduinoHub::DetectDevice(void)
{
   if (initialized_)
      return MM::CanCommunicate;

   // all conditions must be satisfied...
   MM::DeviceDetectionStatus result = MM::Misconfigured;
   char answerTO[MM::MaxStrLength];
   
   try
   {
      std::string portLowerCase = port_;
      for( std::string::iterator its = portLowerCase.begin(); its != portLowerCase.end(); ++its)
      {
         *its = (char)tolower(*its);
      }
      if( 0< portLowerCase.length() &&  0 != portLowerCase.compare("undefined")  && 0 != portLowerCase.compare("unknown") )
      {
         result = MM::CanNotCommunicate;
         // record the default answer time out
         GetCoreCallback()->GetDeviceProperty(port_.c_str(), "AnswerTimeout", answerTO);

         // device specific default communication parameters
         // for Arduino Duemilanova
         GetCoreCallback()->SetDeviceProperty(port_.c_str(), MM::g_Keyword_Handshaking, g_Off);
         GetCoreCallback()->SetDeviceProperty(port_.c_str(), MM::g_Keyword_BaudRate, "57600" );
         GetCoreCallback()->SetDeviceProperty(port_.c_str(), MM::g_Keyword_StopBits, "1");
         // Arduino timed out in GetControllerVersion even if AnswerTimeout  = 300 ms
         GetCoreCallback()->SetDeviceProperty(port_.c_str(), "AnswerTimeout", "500.0");
         GetCoreCallback()->SetDeviceProperty(port_.c_str(), "DelayBetweenCharsMs", "0");
         MM::Device* pS = GetCoreCallback()->GetDevice(this, port_.c_str());
         pS->Initialize();
         // The first second or so after opening the serial port, the Arduino is waiting for firmwareupgrades.  Simply sleep 2 seconds.
         CDeviceUtils::SleepMs(2000);
         MMThreadGuard myLock(lock_);
         PurgeComPort(port_.c_str());
         int v = 0;
         int ret = GetControllerVersion(v);
         // later, Initialize will explicitly check the version #
         if( DEVICE_OK != ret )
         {
            LogMessageCode(ret,true);
         }
         else
         {
            // to succeed must reach here....
            result = MM::CanCommunicate;
         }
         pS->Shutdown();
         // always restore the AnswerTimeout to the default
         GetCoreCallback()->SetDeviceProperty(port_.c_str(), "AnswerTimeout", answerTO);

      }
   }
   catch(...)
   {
      LogMessage("Exception in DetectDevice!",false);
   }

   return result;
}


int CArduinoHub::Initialize()
{
   // Name
   int ret = CreateProperty(MM::g_Keyword_Name, g_DeviceNameArduinoHub, MM::String, true);
   if (DEVICE_OK != ret)
      return ret;

   // The first second or so after opening the serial port, the Arduino is waiting for firmwareupgrades.  Simply sleep 1 second.
   CDeviceUtils::SleepMs(2000);

   MMThreadGuard myLock(lock_);

   // Check that we have a controller:
   PurgeComPort(port_.c_str());
   ret = GetControllerVersion(version_);
   if( DEVICE_OK != ret)
      return ret;

   if (version_ < g_Min_MMVersion || version_ > g_Max_MMVersion)
      return ERR_VERSION_MISMATCH;

   CPropertyAction* pAct = new CPropertyAction(this, &CArduinoHub::OnVersion);
   std::ostringstream sversion;
   sversion << version_;
   CreateProperty(g_versionProp, sversion.str().c_str(), MM::Integer, true, pAct);
   
    // Test if the controller accepts compressed sensing
    if (version_ == cs_version_allowed_) {
        ret = GetCSMode(cs_firmware_);
        if( DEVICE_OK != ret)
            return ret;       
    }
   
    // CS enabled property
    if (cs_firmware_) {
        // CS Mode
        pAct = new CPropertyAction(this, &CArduinoHub::OnCSOnOff);
        CreateProperty("Compressed sensing", "0", MM::Integer, false, pAct, false);
        
        // TODOCreate new property here
        pAct = new CPropertyAction(this, &CArduinoHub::OnExposureChanged);
        CreateProperty("Arduino Exposure", "1", MM::Integer, false, pAct, false);
        
        CreateProperty("CSEnabled", "true", MM::String, true);
    } else {
        CreateProperty("CSEnabled", "false", MM::String, true);
    }
        
    // Get basis id (hashCode)
    pAct = new CPropertyAction(this, &CArduinoHub::OnVersion);
    if (cs_firmware_) {
        // Check the basis Id here
        GetCSBasisId(cs_basis_id_);
        std::ostringstream sbasis_id;
        sbasis_id << cs_basis_id_;
        CreateProperty("CSBasisId", sbasis_id.str().c_str(), MM::Integer, pAct); 
    }

   ret = UpdateStatus();
   if (ret != DEVICE_OK)
      return ret;

   // turn off verbose serial debug messages
   // GetCoreCallback()->SetDeviceProperty(port_.c_str(), "Verbose", "0");

   initialized_ = true;
   return DEVICE_OK;
}

int CArduinoHub::DetectInstalledDevices()
{
   if (MM::CanCommunicate == DetectDevice()) 
   {
      std::vector<std::string> peripherals; 
      peripherals.clear();
      peripherals.push_back(g_DeviceNameArduinoSwitch);
      peripherals.push_back(g_DeviceNameArduinoShutter);
      peripherals.push_back(g_DeviceNameArduinoInput);
      peripherals.push_back(g_DeviceNameArduinoDA1);
      peripherals.push_back(g_DeviceNameArduinoDA2);
      for (size_t i=0; i < peripherals.size(); i++) 
      {
         MM::Device* pDev = ::CreateDevice(peripherals[i].c_str());
         if (pDev) 
         {
            AddInstalledDevice(pDev);
         }
      }
   }

   return DEVICE_OK;
}

int CArduinoHub::Shutdown()
{
   initialized_ = false;
   return DEVICE_OK;
}

int CArduinoHub::OnPort(MM::PropertyBase* pProp, MM::ActionType pAct)
{
   if (pAct == MM::BeforeGet)
   {
      pProp->Set(port_.c_str());
   }
   else if (pAct == MM::AfterSet)
   {
      pProp->Get(port_);
      portAvailable_ = true;
   }
   return DEVICE_OK;
}

int CArduinoHub::OnVersion(MM::PropertyBase* pProp, MM::ActionType pAct)
{
   if (pAct == MM::BeforeGet)
   {
      pProp->Set((long)version_);
   }
   return DEVICE_OK;
}

int CArduinoHub::OnLogic(MM::PropertyBase* pProp, MM::ActionType pAct)
{
   if (pAct == MM::BeforeGet)
   {
      if (invertedLogic_)
         pProp->Set(g_invertedLogicString);
      else
         pProp->Set(g_normalLogicString);
   } else if (pAct == MM::AfterSet)
   {
      std::string logic;
      pProp->Get(logic);
      if (logic.compare(g_invertedLogicString)==0)
         invertedLogic_ = true;
      else invertedLogic_ = false;
   }
   return DEVICE_OK;
}

/* Should set the exposure property to the Arduino */
int CArduinoHub::OnExposureChanged(MM::PropertyBase* pProp, MM::ActionType eAct) {
    if (eAct == MM::AfterSet) { // If the property is being edited, send the new value to the Arduino 
        LogMessage("Property modified", false);
        
        long cson;
        pProp->Get(cson);    
        std::stringstream ss;
        ss << cson;
        LogMessage(ss.str());
        
        // request the state to the Arduino
        unsigned char command[4];
        command[0] = 52;
        // a3=a%256;a2=(a-a3)/256.%256;a1=(a-a2*256-a3)/(256.*256)%256; print int(a1),int(a2),int(a3)

        command[3] = (unsigned char) cson % 256;
        command[2] = (unsigned char) ((cson - command[3])/256) % 256;
        command[1] = (unsigned char) ((cson - command[2]*256-command[3])/(256*256)) % 256;
        
        int ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 4);
        if (ret != DEVICE_OK)
            return ret;

        // read the answer
        std::string answer;
        ret = GetSerialAnswer(port_.c_str(), "\r\n", answer);
        if (ret != DEVICE_OK) {
            return ret;
        }
        LogMessage(answer); 
        if (answer != std::string("4")+ss.str()) { // Answer is gonna be the concatenations
            return ERR_COMMUNICATION;   
        }           
    }
    return DEVICE_OK;
}

/* Should set ON or OFF the CS mode*/
int CArduinoHub::OnCSOnOff(MM::PropertyBase* pProp, MM::ActionType eAct) 
{
    if (eAct == MM::BeforeGet) // Do this when the property is being requested
    {
        LogMessage("Property queried", false);
        
        // request the state to the Arduino
        unsigned char command[1];
        command[0] = 51;
        int ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 1);
        if (ret != DEVICE_OK)
            return ret;

        // read the answer
        std::string answer;
        ret = GetSerialAnswer(port_.c_str(), "\r\n", answer);
        if (ret != DEVICE_OK) {
            return ret;
        }
        
        // analyze the answer
        LogMessage(answer); 

        if (answer == "30") { // CS activated
            pProp->Set("0");
        } else if (answer == "31") { // CS deactivated
            pProp->Set("1");
        } else {
            return ERR_COMMUNICATION;   
        }   
    } 
    else if (eAct == MM::AfterSet) // If the property is being edited, send the new value to the Arduino 
    {
        LogMessage("Property modified", false);
        
        long cson;
        pProp->Get(cson);    
        std::stringstream ss;
        ss << cson;
        LogMessage(ss.str());
        
        // request the state to the Arduino
        unsigned char command[2];
        command[0] = 50;
        command[1] = cson;
        int ret = WriteToComPort(port_.c_str(), (const unsigned char*) command, 2);
        if (ret != DEVICE_OK)
            return ret;

        // read the answer
        std::string answer;
        ret = GetSerialAnswer(port_.c_str(), "\r\n", answer);
        if (ret != DEVICE_OK) {
            return ret;
        }
        LogMessage(answer); 
        if (answer != "2") { // CS activated
            return ERR_COMMUNICATION;   
        }           
    }
    return DEVICE_OK;
}
///////////////////////////////////////////////////////////////////////////////
// CArduinoSwitch implementation
// ~~~~~~~~~~~~~~~~~~~~~~~~~~

CArduinoSwitch::CArduinoSwitch() : 
   nrPatternsUsed_(0),
   currentDelay_(0),
   sequenceOn_(false),
   blanking_(false),
   initialized_(false),
   numPos_(64), 
   busy_(false)
{
   InitializeDefaultErrorMessages();

   // add custom error messages
   SetErrorText(ERR_UNKNOWN_POSITION, "Invalid position (state) specified");
   SetErrorText(ERR_INITIALIZE_FAILED, "Initialization of the device failed");
   SetErrorText(ERR_WRITE_FAILED, "Failed to write data to the device");
   SetErrorText(ERR_CLOSE_FAILED, "Failed closing the device");
   SetErrorText(ERR_COMMUNICATION, "Error in communication with Arduino board");
   SetErrorText(ERR_NO_PORT_SET, "Hub Device not found.  The Arduino Hub device is needed to create this device");

   for (unsigned int i=0; i < NUMPATTERNS; i++)
      pattern_[i] = 0;

   // Description
   int ret = CreateProperty(MM::g_Keyword_Description, "Arduino digital output driver", MM::String, true);
   assert(DEVICE_OK == ret);

   // Name
   ret = CreateProperty(MM::g_Keyword_Name, g_DeviceNameArduinoSwitch, MM::String, true);
   assert(DEVICE_OK == ret);

   // parent ID display
   CreateHubIDProperty();

}

CArduinoSwitch::~CArduinoSwitch()
{
   Shutdown();
}

void CArduinoSwitch::GetName(char* name) const
{
   CDeviceUtils::CopyLimitedString(name, g_DeviceNameArduinoSwitch);
}


int CArduinoSwitch::Initialize()
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable()) {
      return ERR_NO_PORT_SET;
   }
   char hubLabel[MM::MaxStrLength];
   hub->GetLabel(hubLabel);
   SetParentID(hubLabel); // for backward comp.

   // set property list
   // -----------------
   
   // create positions and labels
   const int bufSize = 65;
   char buf[bufSize];
   for (long i=0; i<numPos_; i++)
   {
      snprintf(buf, bufSize, "%d", (unsigned)i);
      SetPositionLabel(i, buf);
   }

   // State
   // -----
   CPropertyAction* pAct = new CPropertyAction (this, &CArduinoSwitch::OnState);
   int nRet = CreateProperty(MM::g_Keyword_State, "0", MM::Integer, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   SetPropertyLimits(MM::g_Keyword_State, 0, numPos_ - 1);

   // Label
   // -----
   pAct = new CPropertyAction (this, &CStateBase::OnLabel);
   nRet = CreateProperty(MM::g_Keyword_Label, "", MM::String, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;

   pAct = new CPropertyAction(this, &CArduinoSwitch::OnSequence);
   nRet = CreateProperty("Sequence", g_On, MM::String, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   AddAllowedValue("Sequence", g_On);
   AddAllowedValue("Sequence", g_Off);

   // Starts "blanking" mode: goal is to synchronize laser light with camera exposure
   std::string blankMode = "Blanking Mode";
   pAct = new CPropertyAction(this, &CArduinoSwitch::OnBlanking);
   nRet = CreateProperty(blankMode.c_str(), "Idle", MM::String, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   AddAllowedValue(blankMode.c_str(), g_On);
   AddAllowedValue(blankMode.c_str(), g_Off);

   // Blank on TTL high or low
   pAct = new CPropertyAction(this, &CArduinoSwitch::OnBlankingTriggerDirection);
   nRet = CreateProperty("Blank On", "Low", MM::String, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   AddAllowedValue("Blank On", "Low");
   AddAllowedValue("Blank On", "High");

   /*
   // Starts producing timed digital output patterns 
   // Parameters that influence the pattern are 'Repeat Timed Pattern', 'Delay', 'State' where the latter two are manipulated with the Get and SetPattern functions
   std::string timedOutput = "Timed Output Mode";
   pAct = new CPropertyAction(this, &CArduinoSwitch::OnStartTimedOutput);
   nRet = CreateProperty(timedOutput.c_str(), "Idle", MM::String, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   AddAllowedValue(timedOutput.c_str(), "Stop");
   AddAllowedValue(timedOutput.c_str(), "Start");
   AddAllowedValue(timedOutput.c_str(), "Running");
   AddAllowedValue(timedOutput.c_str(), "Idle");

   // Sets a delay (in ms) to be used in timed output mode
   // This delay will be transferred to the Arduino using the Get and SetPattern commands
   pAct = new CPropertyAction(this, &CArduinoSwitch::OnDelay);
   nRet = CreateProperty("Delay (ms)", "0", MM::Integer, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   SetPropertyLimits("Delay (ms)", 0, 65535);

   // Repeat the timed Pattern this many times:
   pAct = new CPropertyAction(this, &CArduinoSwitch::OnRepeatTimedPattern);
   nRet = CreateProperty("Repeat Timed Pattern", "0", MM::Integer, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   SetPropertyLimits("Repeat Timed Pattern", 0, 255);
   */

   nRet = UpdateStatus();
   if (nRet != DEVICE_OK)
      return nRet;

   initialized_ = true;

   return DEVICE_OK;
}

int CArduinoSwitch::Shutdown()
{
   initialized_ = false;
   return DEVICE_OK;
}

int CArduinoSwitch::WriteToPort(long value)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable()) {
      return ERR_NO_PORT_SET;
   }

   MMThreadGuard myLock(hub->GetLock());

   value = 63 & value;
   if (hub->IsLogicInverted())
      value = ~value;

   hub->PurgeComPortH();

   unsigned char command[2];
   command[0] = 1;
   command[1] = (unsigned char) value;
   int ret = hub->WriteToComPortH((const unsigned char*) command, 2);
   if (ret != DEVICE_OK)
      return ret;

   MM::MMTime startTime = GetCurrentMMTime();
   unsigned long bytesRead = 0;
   unsigned char answer[1];
   while ((bytesRead < 1) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
      ret = hub->ReadFromComPortH(answer, 1, bytesRead);
      if (ret != DEVICE_OK)
         return ret;
   }
   if (answer[0] != 1)
      return ERR_COMMUNICATION;

   hub->SetTimedOutput(false);

   return DEVICE_OK;
}

int CArduinoSwitch::LoadSequence(unsigned size, unsigned char* seq)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   hub->PurgeComPortH();

   for (unsigned i=0; i < size; i++)
   {
      unsigned char value = seq[i];

      value = 63 & value;
      if (hub->IsLogicInverted())
         value = ~value;

      unsigned char command[3];
      command[0] = 5;
      command[1] = (unsigned char) i;
      command[2] = value;
      int ret = hub->WriteToComPortH((const unsigned char*) command, 3);
      if (ret != DEVICE_OK)
         return ret;


      MM::MMTime startTime = GetCurrentMMTime();
      unsigned long bytesRead = 0;
      unsigned char answer[3];
      while ((bytesRead < 3) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
         unsigned long br;
         ret = hub->ReadFromComPortH(answer + bytesRead, 3, br);
      if (ret != DEVICE_OK)
         return ret;
      bytesRead += br;
      }
      if (answer[0] != 5)
         return ERR_COMMUNICATION;

   }

   unsigned char command[2];
   command[0] = 6;
   command[1] = (unsigned char) size;
   int ret = hub->WriteToComPortH((const unsigned char*) command, 2);
   if (ret != DEVICE_OK)
      return ret;

   MM::MMTime startTime = GetCurrentMMTime();
   unsigned long bytesRead = 0;
   unsigned char answer[2];
   while ((bytesRead < 2) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
      unsigned long br;
      ret = hub->ReadFromComPortH(answer + bytesRead, 2, br);
      if (ret != DEVICE_OK)
         return ret;
      bytesRead += br;
   }
   if (answer[0] != 6)
      return ERR_COMMUNICATION;

   return DEVICE_OK;
}

///////////////////////////////////////////////////////////////////////////////
// Action handlers
///////////////////////////////////////////////////////////////////////////////

int CArduinoSwitch::OnState(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   if (eAct == MM::BeforeGet)
   {
      // nothing to do, let the caller use cached property
   }
   else if (eAct == MM::AfterSet)
   {
      long pos;
      pProp->Get(pos);
      hub->SetSwitchState(pos);
      if (hub->GetShutterState() > 0)
         return WriteToPort(pos);
   }
   else if (eAct == MM::IsSequenceable)                                      
   {                                                                         
      if (sequenceOn_)                                                       
         pProp->SetSequenceable(NUMPATTERNS);                           
      else                                                                   
         pProp->SetSequenceable(0);                                          
   } 
   else if (eAct == MM::AfterLoadSequence)                                   
   {                                                                         
      std::vector<std::string> sequence = pProp->GetSequence();              
      std::ostringstream os;
      if (sequence.size() > NUMPATTERNS)                                
         return DEVICE_SEQUENCE_TOO_LARGE;                                   
      unsigned char* seq = new unsigned char[sequence.size()];               
      for (unsigned int i=0; i < sequence.size(); i++)                       
      {
         std::istringstream os (sequence[i]);
         int val;
         os >> val;
         seq[i] = (unsigned char) val;
      }                                                                      
      int ret = LoadSequence((unsigned) sequence.size(), seq);
      if (ret != DEVICE_OK)                                                  
         return ret;                                                         
                                                                             
      delete[] seq;                                                          
   }                                                                         
   else if (eAct == MM::StartSequence)
   { 
      MMThreadGuard myLock(hub->GetLock());

      hub->PurgeComPortH();
      unsigned char command[1];
      command[0] = 8;
      int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
      if (ret != DEVICE_OK)
         return ret;

      MM::MMTime startTime = GetCurrentMMTime();
      unsigned long bytesRead = 0;
      unsigned char answer[1];
      while ((bytesRead < 1) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
         unsigned long br;
         ret = hub->ReadFromComPortH(answer + bytesRead, 1, br);
         if (ret != DEVICE_OK)
            return ret;
         bytesRead += br;
      }
      if (answer[0] != 8)
         return ERR_COMMUNICATION;
   }
   else if (eAct == MM::StopSequence)                                        
   {
      MMThreadGuard myLock(hub->GetLock());

      unsigned char command[1];
      command[0] = 9;
      int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
      if (ret != DEVICE_OK)
         return ret;

      MM::MMTime startTime = GetCurrentMMTime();
      unsigned long bytesRead = 0;
      unsigned char answer[2];
      while ((bytesRead < 2) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
         unsigned long br;
         ret = hub->ReadFromComPortH(answer + bytesRead, 2, br);
         if (ret != DEVICE_OK)
            return ret;
         bytesRead += br;
      }
      if (answer[0] != 9)
         return ERR_COMMUNICATION;

      std::ostringstream os;
      os << "Sequence had " << (int) answer[1] << " transitions";
      LogMessage(os.str().c_str(), false);

   }                                                                         

   return DEVICE_OK;
}

int CArduinoSwitch::OnSequence(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      if (sequenceOn_)
         pProp->Set(g_On);
      else
         pProp->Set(g_Off);
   }
   else if (eAct == MM::AfterSet)
   {
      std::string state;
      pProp->Get(state);
      if (state == g_On)
         sequenceOn_ = true;
      else
         sequenceOn_ = false;
   }
   return DEVICE_OK;
}

// Synchronize the "Running" property with the Arduino
// -- Send command 12 when acquisition is running
// -- Send command  9 when acquisition is Idle
int CArduinoSwitch::OnStartTimedOutput(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   if (eAct == MM::BeforeGet) {
      if (hub->IsTimedOutputActive())
         pProp->Set("Running");
      else
         pProp->Set("Idle");
   }
   else if (eAct == MM::AfterSet)
   {
      MMThreadGuard myLock(hub->GetLock());

      std::string prop;
      pProp->Get(prop);

      if (prop =="Start") {
         hub->PurgeComPortH();
         unsigned char command[1];
         command[0] = 12;
         int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
         if (ret != DEVICE_OK)
            return ret;

         MM::MMTime startTime = GetCurrentMMTime();
         unsigned long bytesRead = 0;
         unsigned char answer[1];
         while ((bytesRead < 1) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
            unsigned long br;
            ret = hub->ReadFromComPortH(answer + bytesRead, 1, br);
            if (ret != DEVICE_OK)
               return ret;
            bytesRead += br;
         }
         if (answer[0] != 12)
            return ERR_COMMUNICATION;
         hub->SetTimedOutput(true);
      } else {
         unsigned char command[1];
         command[0] = 9;
         int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
         if (ret != DEVICE_OK)
            return ret;

         MM::MMTime startTime = GetCurrentMMTime();
         unsigned long bytesRead = 0;
         unsigned char answer[2];
         while ((bytesRead < 2) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
            unsigned long br;
            ret = hub->ReadFromComPortH(answer + bytesRead, 2, br);
            if (ret != DEVICE_OK)
               return ret;
            bytesRead += br;
         }
         if (answer[0] != 9)
            return ERR_COMMUNICATION;
         hub->SetTimedOutput(false);
      }
   }

   return DEVICE_OK;
}

// Synchronize the Arduino wrt the blanking state
// Uses command 20 to switch blanking On
// Uses command 21 to switch blanking Off
int CArduinoSwitch::OnBlanking(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   if (eAct == MM::BeforeGet) {
      if (blanking_)
         pProp->Set(g_On);
      else
         pProp->Set(g_Off);
   }
   else if (eAct == MM::AfterSet)
   {
      MMThreadGuard myLock(hub->GetLock());

      std::string prop;
      pProp->Get(prop);

      if (prop == g_On && !blanking_) {
         hub->PurgeComPortH();
         unsigned char command[1];
         command[0] = 20;
         int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
         if (ret != DEVICE_OK)
            return ret;

         MM::MMTime startTime = GetCurrentMMTime();
         unsigned long bytesRead = 0;
         unsigned char answer[1];
         while ((bytesRead < 1) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
            unsigned long br;
            ret = hub->ReadFromComPortH(answer + bytesRead, 1, br);
            if (ret != DEVICE_OK)
               return ret;
            bytesRead += br;
         }
         if (answer[0] != 20)
            return ERR_COMMUNICATION;
         blanking_ = true;
         hub->SetTimedOutput(false);
         LogMessage("Switched blanking on", true);

      } else if (prop == g_Off && blanking_){
         unsigned char command[1];
         command[0] = 21;
         int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
         if (ret != DEVICE_OK)
            return ret;

         MM::MMTime startTime = GetCurrentMMTime();
         unsigned long bytesRead = 0;
         unsigned char answer[2];
         while ((bytesRead < 2) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
            unsigned long br;
            ret = hub->ReadFromComPortH(answer + bytesRead, 2, br);
            if (ret != DEVICE_OK)
               return ret;
            bytesRead += br;
         }
         if (answer[0] != 21)
            return ERR_COMMUNICATION;
         blanking_ = false;
         hub->SetTimedOutput(false);
         LogMessage("Switched blanking off", true);
      }
   }

   return DEVICE_OK;
}

int CArduinoSwitch::OnBlankingTriggerDirection(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   if (eAct == MM::BeforeGet) {
      // nothing to do, let the caller use cached property
   }
   else if (eAct == MM::AfterSet)
   {
      MMThreadGuard myLock(hub->GetLock());

      std::string direction;
      pProp->Get(direction);

      hub->PurgeComPortH();
      unsigned char command[2];
      command[0] = 22;
      if (direction == "Low") 
         command[1] = 1;
      else
         command[1] = 0;

      int ret = hub->WriteToComPortH((const unsigned char*) command, 2);
      if (ret != DEVICE_OK)
         return ret;

      MM::MMTime startTime = GetCurrentMMTime();
      unsigned long bytesRead = 0;
      unsigned char answer[1];
      while ((bytesRead < 1) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
         unsigned long br;
         ret = hub->ReadFromComPortH(answer + bytesRead, 1, br);
         if (ret != DEVICE_OK)
            return ret;
         bytesRead += br;
      }
      if (answer[0] != 22)
         return ERR_COMMUNICATION;

   }

   return DEVICE_OK;
}

int CArduinoSwitch::OnDelay(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet) {
      pProp->Set((long int)currentDelay_);
   }
   else if (eAct == MM::AfterSet)
   {
      long prop;
      pProp->Get(prop);
      currentDelay_ = (int) prop;
   }

   return DEVICE_OK;
}

int CArduinoSwitch::OnRepeatTimedPattern(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   if (eAct == MM::BeforeGet) {
   }
   else if (eAct == MM::AfterSet)
   {
      MMThreadGuard myLock(hub->GetLock());

      long prop;
      pProp->Get(prop);

      hub->PurgeComPortH();
      unsigned char command[2];
      command[0] = 11;
      command[1] = (unsigned char) prop;

      int ret = hub->WriteToComPortH((const unsigned char*) command, 2);
      if (ret != DEVICE_OK)
         return ret;

      MM::MMTime startTime = GetCurrentMMTime();
      unsigned long bytesRead = 0;
      unsigned char answer[2];
      while ((bytesRead < 2) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
         unsigned long br;
         ret = hub->ReadFromComPortH(answer + bytesRead, 2, br);
         if (ret != DEVICE_OK)
            return ret;
         bytesRead += br;
      }
      if (answer[0] != 11)
         return ERR_COMMUNICATION;

      hub->SetTimedOutput(false);
   }

   return DEVICE_OK;
}

///////////////////////////////////////////////////////////////////////////////
// CArduinoDA implementation
// ~~~~~~~~~~~~~~~~~~~~~~

CArduinoDA::CArduinoDA(int channel) :
      busy_(false), 
      minV_(0.0), 
      maxV_(5.0), 
      volts_(0.0),
      gatedVolts_(0.0),
      channel_(channel), 
      maxChannel_(2),
      gateOpen_(true)
{
   InitializeDefaultErrorMessages();

   // add custom error messages
   SetErrorText(ERR_UNKNOWN_POSITION, "Invalid position (state) specified");
   SetErrorText(ERR_INITIALIZE_FAILED, "Initialization of the device failed");
   SetErrorText(ERR_WRITE_FAILED, "Failed to write data to the device");
   SetErrorText(ERR_CLOSE_FAILED, "Failed closing the device");
   SetErrorText(ERR_NO_PORT_SET, "Hub Device not found.  The Arduino Hub device is needed to create this device");

   /* Channel property is not needed
   CPropertyAction* pAct = new CPropertyAction(this, &CArduinoDA::OnChannel);
   CreateProperty("Channel", channel_ == 1 ? "1" : "2", MM::Integer, false, pAct, true);
   for (int i=1; i<= 2; i++){
      std::ostringstream os;
      os << i;
      AddAllowedValue("Channel", os.str().c_str());
   }
   */

   CPropertyAction* pAct = new CPropertyAction(this, &CArduinoDA::OnMaxVolt);
   CreateProperty("MaxVolt", "5.0", MM::Float, false, pAct, true);

   name_ = channel_ == 1 ? g_DeviceNameArduinoDA1 : g_DeviceNameArduinoDA2;

   // Description
   int nRet = CreateProperty(MM::g_Keyword_Description, "Arduino DAC driver", MM::String, true);
   assert(DEVICE_OK == nRet);

   // Name
   nRet = CreateProperty(MM::g_Keyword_Name, name_.c_str(), MM::String, true);
   assert(DEVICE_OK == nRet);

   // parent ID display
   CreateHubIDProperty();
}

CArduinoDA::~CArduinoDA()
{
   Shutdown();
}

void CArduinoDA::GetName(char* name) const
{
   CDeviceUtils::CopyLimitedString(name, name_.c_str());
}


int CArduinoDA::Initialize()
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable()) {
      return ERR_NO_PORT_SET;
   }
   char hubLabel[MM::MaxStrLength];
   hub->GetLabel(hubLabel);
   SetParentID(hubLabel); // for backward comp.

   // set property list
   // -----------------
   
   // State
   // -----
   CPropertyAction* pAct = new CPropertyAction (this, &CArduinoDA::OnVolts);
   int nRet = CreateProperty("Volts", "0.0", MM::Float, false, pAct);
   if (nRet != DEVICE_OK)
      return nRet;
   SetPropertyLimits("Volts", minV_, maxV_);

   nRet = UpdateStatus();
   if (nRet != DEVICE_OK)
      return nRet;

   initialized_ = true;

   return DEVICE_OK;
}

int CArduinoDA::Shutdown()
{
   initialized_ = false;
   return DEVICE_OK;
}

int CArduinoDA::WriteToPort(unsigned long value)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   MMThreadGuard myLock(hub->GetLock());

   hub->PurgeComPortH();

   unsigned char command[4];
   command[0] = 3;
   command[1] = (unsigned char) (channel_ -1);
   command[2] = (unsigned char) (value / 256L);
   command[3] = (unsigned char) (value & 255);
   int ret = hub->WriteToComPortH((const unsigned char*) command, 4);
   if (ret != DEVICE_OK)
      return ret;

   MM::MMTime startTime = GetCurrentMMTime();
   unsigned long bytesRead = 0;
   unsigned char answer[4];
   while ((bytesRead < 4) && ( (GetCurrentMMTime() - startTime).getMsec() < 2500)) {
      unsigned long bR;
      ret = hub->ReadFromComPortH(answer + bytesRead, 4 - bytesRead, bR);
      if (ret != DEVICE_OK)
         return ret;
      bytesRead += bR;
   }
   
   if (answer[0] != 3)
      return ERR_COMMUNICATION;

   hub->SetTimedOutput(false);

   return DEVICE_OK;
}


int CArduinoDA::WriteSignal(double volts)
{
   //long value = (long) ( (volts - minV_) / maxV_ * 4095);
   long value = (long) ( (volts - minV_) / maxV_ * 255); // 8 bits DAC
   
   std::ostringstream os;
    os << "8BIT DAC ONLY -- Volts: " << volts << " Max Voltage: " << maxV_ << " digital value: " << value;
    LogMessage(os.str().c_str(), false); // used to be true

   return WriteToPort(value);
}

int CArduinoDA::SetSignal(double volts)
{
   volts_ = volts;
   if (gateOpen_) {
      gatedVolts_ = volts_;
      return WriteSignal(volts_);
   } else {
      gatedVolts_ = 0;
   }

   return DEVICE_OK;
}

int CArduinoDA::SetGateOpen(bool open)
{
   if (open) {
      gateOpen_ = true;
      gatedVolts_ = volts_;
      return WriteSignal(volts_);
   } 
   gateOpen_ = false;
   gatedVolts_ = 0;
   return WriteSignal(0.0);

}

///////////////////////////////////////////////////////////////////////////////
// Action handlers
///////////////////////////////////////////////////////////////////////////////

int CArduinoDA::OnVolts(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      // nothing to do, let the caller use cached property
   }
   else if (eAct == MM::AfterSet)
   {
      double volts;
      pProp->Get(volts);
      return SetSignal(volts);
   }

   return DEVICE_OK;
}

int CArduinoDA::OnMaxVolt(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set(maxV_);
   }
   else if (eAct == MM::AfterSet)
   {
      pProp->Get(maxV_);
      if (HasProperty("Volts"))
         SetPropertyLimits("Volts", 0.0, maxV_);

   }
   return DEVICE_OK;
}

int CArduinoDA::OnChannel(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set((long int)channel_);
   }
   else if (eAct == MM::AfterSet)
   {
      long channel;
      pProp->Get(channel);
      if (channel >=1 && ( (unsigned) channel <=maxChannel_) )
         channel_ = channel;
   }
   return DEVICE_OK;
}


///////////////////////////////////////////////////////////////////////////////
// CArduinoShutter implementation
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~

CArduinoShutter::CArduinoShutter() : initialized_(false), name_(g_DeviceNameArduinoShutter)
{
   InitializeDefaultErrorMessages();
   EnableDelay();

   SetErrorText(ERR_NO_PORT_SET, "Hub Device not found.  The Arduino Hub device is needed to create this device");

   // Name
   int ret = CreateProperty(MM::g_Keyword_Name, g_DeviceNameArduinoShutter, MM::String, true);
   assert(DEVICE_OK == ret);

   // Description
   ret = CreateProperty(MM::g_Keyword_Description, "Arduino shutter driver", MM::String, true);
   assert(DEVICE_OK == ret);

   // parent ID display
   CreateHubIDProperty();
}

CArduinoShutter::~CArduinoShutter()
{
   Shutdown();
}

void CArduinoShutter::GetName(char* name) const
{
   CDeviceUtils::CopyLimitedString(name, g_DeviceNameArduinoShutter);
}

bool CArduinoShutter::Busy()
{
   MM::MMTime interval = GetCurrentMMTime() - changedTime_;

   if (interval < (1000.0 * GetDelayMs() ))
      return true;
   else
       return false;
}

int CArduinoShutter::Initialize()
{

   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable()) {
      return ERR_NO_PORT_SET;
   }
   char hubLabel[MM::MaxStrLength];
   hub->GetLabel(hubLabel);
   SetParentID(hubLabel); // for backward comp.

   // set property list
   // -----------------
   
   // OnOff
   // ------
   CPropertyAction* pAct = new CPropertyAction (this, &CArduinoShutter::OnOnOff);
   int ret = CreateProperty("OnOff", "0", MM::Integer, false, pAct);
   if (ret != DEVICE_OK)
      return ret;

   // set shutter into the off state
   //WriteToPort(0);

   std::vector<std::string> vals;
   vals.push_back("0");
   vals.push_back("1");
   ret = SetAllowedValues("OnOff", vals);
   if (ret != DEVICE_OK)
      return ret;

   ret = UpdateStatus();
   if (ret != DEVICE_OK)
      return ret;

   changedTime_ = GetCurrentMMTime();
   initialized_ = true;

   return DEVICE_OK;
}

int CArduinoShutter::Shutdown()
{
   if (initialized_)
   {
      initialized_ = false;
   }
   return DEVICE_OK;
}

int CArduinoShutter::SetOpen(bool open)
{
	std::ostringstream os;
	os << "Request " << open;
	LogMessage(os.str().c_str(), true);

   if (open)
      return SetProperty("OnOff", "1");
   else
      return SetProperty("OnOff", "0");
}

int CArduinoShutter::GetOpen(bool& open)
{
   char buf[MM::MaxStrLength];
   int ret = GetProperty("OnOff", buf);
   if (ret != DEVICE_OK)
      return ret;
   long pos = atol(buf);
   pos > 0 ? open = true : open = false;

   return DEVICE_OK;
}

int CArduinoShutter::Fire(double /*deltaT*/)
{
   return DEVICE_UNSUPPORTED_COMMAND;
}

int CArduinoShutter::WriteToPort(long value)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   MMThreadGuard myLock(hub->GetLock());

   value = 63 & value;
   if (hub->IsLogicInverted())
      value = ~value;

   hub->PurgeComPortH();

   unsigned char command[2];
   command[0] = 1;
   command[1] = (unsigned char) value;
   int ret = hub->WriteToComPortH((const unsigned char*) command, 2);
   if (ret != DEVICE_OK)
      return ret;

   MM::MMTime startTime = GetCurrentMMTime();
   unsigned long bytesRead = 0;
   unsigned char answer[1];
   while ((bytesRead < 1) && ( (GetCurrentMMTime() - startTime).getMsec() < 250)) {
      ret = hub->ReadFromComPortH(answer, 1, bytesRead);
      if (ret != DEVICE_OK)
         return ret;
   }
   if (answer[0] != 1)
      return ERR_COMMUNICATION;

   hub->SetTimedOutput(false);

   return DEVICE_OK;
}

///////////////////////////////////////////////////////////////////////////////
// Action handlers
///////////////////////////////////////////////////////////////////////////////

int CArduinoShutter::OnOnOff(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (eAct == MM::BeforeGet)
   {
      // use cached state
      pProp->Set((long)hub->GetShutterState());
   }
   else if (eAct == MM::AfterSet)
   {
      long pos;
      pProp->Get(pos);
      int ret;
      if (pos == 0)
         ret = WriteToPort(0); // turn everything off
      else
         ret = WriteToPort(hub->GetSwitchState()); // restore old setting
      if (ret != DEVICE_OK)
         return ret;
      hub->SetShutterState(pos);
      changedTime_ = GetCurrentMMTime();
   }

   return DEVICE_OK;
}


/*
 * Arduino input.  Can either be for all pins (0-6)
 * or for an individual pin only
 */

CArduinoInput::CArduinoInput() :
   mThread_(0),
   pin_(0),
   name_(g_DeviceNameArduinoInput)
{
   std::string errorText = "To use the Input function you need firmware version 2 or higher";
   SetErrorText(ERR_VERSION_MISMATCH, errorText.c_str());

   CreateProperty("Pin", "All", MM::String, false, 0, true);
   AddAllowedValue("Pin", "All");
   AddAllowedValue("Pin", "0");
   AddAllowedValue("Pin", "1");
   AddAllowedValue("Pin", "2");
   AddAllowedValue("Pin", "3");
   AddAllowedValue("Pin", "4");
   AddAllowedValue("Pin", "5");

   CreateProperty("Pull-Up-Resistor", g_On, MM::String, false, 0, true);
   AddAllowedValue("Pull-Up-Resistor", g_On);
   AddAllowedValue("Pull-Up-Resistor", g_Off);

   // Name
   int ret = CreateProperty(MM::g_Keyword_Name, name_.c_str(), MM::String, true);
   assert(DEVICE_OK == ret);

   // Description
   ret = CreateProperty(MM::g_Keyword_Description, "Arduino shutter driver", MM::String, true);
   assert(DEVICE_OK == ret);

   // parent ID display
   CreateHubIDProperty();
}

CArduinoInput::~CArduinoInput()
{
   Shutdown();
}

int CArduinoInput::Shutdown()
{
   if (initialized_)
      delete(mThread_);
   initialized_ = false;
   return DEVICE_OK;
}

int CArduinoInput::Initialize()
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable()) {
      return ERR_NO_PORT_SET;
   }
   char hubLabel[MM::MaxStrLength];
   hub->GetLabel(hubLabel);
   SetParentID(hubLabel); // for backward comp.

   char ver[MM::MaxStrLength] = "0";
   hub->GetProperty(g_versionProp, ver);
   int version = atoi(ver);
   if (version < 2)
      return ERR_VERSION_MISMATCH;

   int ret = GetProperty("Pin", pins_);
   if (ret != DEVICE_OK)
      return ret;
   if (strcmp("All", pins_) != 0)
      pin_ = atoi(pins_); 

   ret = GetProperty("Pull-Up-Resistor", pullUp_);
   if (ret != DEVICE_OK)
      return ret;
 
   // Digital Input
   CPropertyAction* pAct = new CPropertyAction (this, &CArduinoInput::OnDigitalInput);
   ret = CreateProperty("DigitalInput", "0", MM::Integer, true, pAct);
   if (ret != DEVICE_OK)
      return ret;

   int start = 0;
   int end = 5;
   if (strcmp("All", pins_) != 0) {
      start = pin_;
      end = pin_;
   }

   for (long i=start; i <=end; i++) 
   {
      CPropertyActionEx *pExAct = new CPropertyActionEx(this, &CArduinoInput::OnAnalogInput, i);
      std::ostringstream os;
      os << "AnalogInput" << i;
      ret = CreateProperty(os.str().c_str(), "0.0", MM::Float, true, pExAct);
      if (ret != DEVICE_OK)
         return ret;
      // set pull up resistor state for this pin
      if (strcmp(g_On, pullUp_) == 0) {
         SetPullUp(i, 1);
      } else {
         SetPullUp(i, 0);
      }

   }

   mThread_ = new ArduinoInputMonitorThread(*this);
   mThread_->Start();

   initialized_ = true;

   return DEVICE_OK;
}

void CArduinoInput::GetName(char* name) const
{
  CDeviceUtils::CopyLimitedString(name, name_.c_str());
}

bool CArduinoInput::Busy()
{
   return false;
}

int CArduinoInput::GetDigitalInput(long* state)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   MMThreadGuard myLock(hub->GetLock());

   unsigned char command[1];
   command[0] = 40;

   int ret = hub->WriteToComPortH((const unsigned char*) command, 1);
   if (ret != DEVICE_OK)
      return ret;

   unsigned char answer[2];
   ret = ReadNBytes(hub, 2, answer);
   if (ret != DEVICE_OK)
      return ret;

   if (answer[0] != 40)
      return ERR_COMMUNICATION;

   if (strcmp("All", pins_) != 0) {
      answer[1] = answer[1] >> pin_;
      answer[1] &= answer[1] & 1;
   }
   
   *state = (long) answer[1];

   return DEVICE_OK;
}

int CArduinoInput::ReportStateChange(long newState)
{
   std::ostringstream os;
   os << newState;
   return OnPropertyChanged("DigitalInput", os.str().c_str());
}


///////////////////////////////////////////////////////////////////////////////
// Action handlers
///////////////////////////////////////////////////////////////////////////////

int CArduinoInput::OnDigitalInput(MM::PropertyBase*  pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      long state;
      int ret = GetDigitalInput(&state);
      if (ret != DEVICE_OK)
         return ret;

      pProp->Set(state);
   }

   return DEVICE_OK;
}

int CArduinoInput::OnAnalogInput(MM::PropertyBase* pProp, MM::ActionType eAct, long  channel )
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   if (eAct == MM::BeforeGet)
   {
      MMThreadGuard myLock(hub->GetLock());

      unsigned char command[2];
      command[0] = 41;
      command[1] = (unsigned char) channel;

      int ret = hub->WriteToComPortH((const unsigned char*) command, 2);
      if (ret != DEVICE_OK)
         return ret;

      unsigned char answer[4];
      ret = ReadNBytes(hub, 4, answer);
      if (ret != DEVICE_OK)
         return ret;

      if (answer[0] != 41)
         return ERR_COMMUNICATION;
      if (answer[1] != channel)
         return ERR_COMMUNICATION;

      int tmp = answer[2];
      tmp = tmp << 8;
      tmp = tmp | answer[3];

      pProp->Set((long) tmp);
   }
   return DEVICE_OK;
}

int CArduinoInput::SetPullUp(int pin, int state)
{
   CArduinoHub* hub = static_cast<CArduinoHub*>(GetParentHub());
   if (!hub || !hub->IsPortAvailable())
      return ERR_NO_PORT_SET;

   MMThreadGuard myLock(hub->GetLock());

   const int nrChrs = 3;
   unsigned char command[nrChrs];
   command[0] = 42;
   command[1] = (unsigned char) pin;
   command[2] = (unsigned char) state;

   int ret = hub->WriteToComPortH((const unsigned char*) command, nrChrs);
   if (ret != DEVICE_OK)
      return ret;

   unsigned char answer[3];
   ret = ReadNBytes(hub, 3, answer);
   if (ret != DEVICE_OK)
      return ret;

   if (answer[0] != 42)
      return ERR_COMMUNICATION;
   if (answer[1] != pin)
      return ERR_COMMUNICATION;

   return DEVICE_OK;
}


int CArduinoInput::ReadNBytes(CArduinoHub* hub, unsigned int n, unsigned char* answer)
{
   MM::MMTime startTime = GetCurrentMMTime();
   unsigned long bytesRead = 0;
   while ((bytesRead < n) && ( (GetCurrentMMTime() - startTime).getMsec() < 500)) {
      unsigned long bR;
      int ret = hub->ReadFromComPortH(answer + bytesRead, n - bytesRead, bR);
      if (ret != DEVICE_OK)
         return ret;
      bytesRead += bR;
   }

   return DEVICE_OK;
}

ArduinoInputMonitorThread::ArduinoInputMonitorThread(CArduinoInput& aInput) :
   state_(0),
   aInput_(aInput)
{
}

ArduinoInputMonitorThread::~ArduinoInputMonitorThread()
{
   Stop();
   wait();
}

int ArduinoInputMonitorThread::svc() 
{
   while (!stop_)
   {
      long state;
      int ret = aInput_.GetDigitalInput(&state);
      if (ret != DEVICE_OK)
      {
         stop_ = true;
         return ret;
      }

      if (state != state_) 
      {
         aInput_.ReportStateChange(state);
         state_ = state;
      }
      CDeviceUtils::SleepMs(500);
   }
   return DEVICE_OK;
}


void ArduinoInputMonitorThread::Start()
{
   stop_ = false;
   activate();
}

/**************************
 * CArduinoZSTage (ex DAZStage) implementation
 */

CArduinoZStage::CArduinoZStage() :
   DADeviceName_ (""),
   initialized_ (false),
   minDAVolt_ (0.0),
   maxDAVolt_ (5.0),
   minStageVolt_ (0.0),
   maxStageVolt_ (5.0),
   minStagePos_ (0.0),
   maxStagePos_ (250.0),
   pos_ (0.0),
   originPos_ (0.0)
{
   InitializeDefaultErrorMessages();

   SetErrorText(ERR_INVALID_DEVICE_NAME, "Please select a valid DA device");
   SetErrorText(ERR_NO_DA_DEVICE, "No DA Device selected");
   SetErrorText(ERR_VOLT_OUT_OF_RANGE, "The DA Device cannot set the requested voltage");
   SetErrorText(ERR_POS_OUT_OF_RANGE, "The requested position is out of range");
   SetErrorText(ERR_NO_DA_DEVICE_FOUND, "No DA Device loaded");

   // Name                                                                   
   CreateProperty(MM::g_Keyword_Name, g_DeviceNameDAZStage, MM::String, true); 
                                                                             
   // Description                                                            
   CreateProperty(MM::g_Keyword_Description, "ZStage controlled with voltage provided by an Arduino board", MM::String, true);

   CPropertyAction* pAct = new CPropertyAction (this, &CArduinoZStage::OnStageMinVolt);      
   CreateProperty("Stage Low Voltage", "0", MM::Float, false, pAct, true);         

   pAct = new CPropertyAction (this, &CArduinoZStage::OnStageMaxVolt);      
   CreateProperty("Stage High Voltage", "5", MM::Float, false, pAct, true);         

   pAct = new CPropertyAction (this, &CArduinoZStage::OnStageMinPos); 
   CreateProperty(g_PropertyMinUm, "0", MM::Float, false, pAct, true); 

   pAct = new CPropertyAction (this, &CArduinoZStage::OnStageMaxPos);      
   CreateProperty(g_PropertyMaxUm, "250", MM::Float, false, pAct, true);         
}  
 
CArduinoZStage::~CArduinoZStage()
{
}

void CArduinoZStage::GetName(char* Name) const                                       
{                                                                            
   CDeviceUtils::CopyLimitedString(Name, g_DeviceNameDAZStage);                
}                                                                            
                                                                             
int CArduinoZStage::Initialize() 
{
   // get list with available DA devices.  
   // TODO: this is a initialization parameter, which makes it harder for the end-user to set up!
   char deviceName[MM::MaxStrLength];
   availableDAs_.clear();
   unsigned int deviceIterator = 0;
   for(;;)
   {
      GetLoadedDeviceOfType(MM::SignalIODevice, deviceName, deviceIterator++);
      if( 0 < strlen(deviceName))
      {
         availableDAs_.push_back(std::string(deviceName));
      }
      else
         break;
   }



   CPropertyAction* pAct = new CPropertyAction (this, &CArduinoZStage::OnDADevice);      
   std::string defaultDA = "Undefined";
   if (availableDAs_.size() >= 1)
      defaultDA = availableDAs_[0];
   CreateProperty("DA Device", defaultDA.c_str(), MM::String, false, pAct, false);         
   if (availableDAs_.size() >= 1)
      SetAllowedValues("DA Device", availableDAs_);
   else
      return ERR_NO_DA_DEVICE_FOUND;

   // This is needed, otherwise DeviceDA_ is not always set resulting in crashes
   // This could lead to strange problems if multiple DA devices are loaded
   SetProperty("DA Device", defaultDA.c_str());

   pAct = new CPropertyAction (this, &CArduinoZStage::OnPosition);
   CreateProperty(MM::g_Keyword_Position, "0.0", MM::Float, false, pAct);
   double minPos = 0.0;
   int ret = GetProperty(g_PropertyMinUm, minPos);
   assert(ret == DEVICE_OK);
   double maxPos = 0.0;
   ret = GetProperty(g_PropertyMaxUm, maxPos);
   assert(ret == DEVICE_OK);
   SetPropertyLimits(MM::g_Keyword_Position, minPos, maxPos);

   ret = UpdateStatus();
   if (ret != DEVICE_OK)
      return ret;

   std::ostringstream tmp;
   tmp << DADevice_;
   LogMessage(tmp.str().c_str());

   if (DADevice_ != 0)
      DADevice_->GetLimits(minDAVolt_, maxDAVolt_);

   if (minStageVolt_ < minDAVolt_)
      return ERR_VOLT_OUT_OF_RANGE;

   originPos_ = minStagePos_;

   initialized_ = true;

   return DEVICE_OK;
}

int CArduinoZStage::Shutdown()
{
   if (initialized_)
      initialized_ = false;

   return DEVICE_OK;
}

bool CArduinoZStage::Busy()
{
   if (DADevice_ != 0)
      return DADevice_->Busy();

   // If we are here, there is a problem.  No way to report it.
   return false;
}

/*
 * Sets the position of the stage in um relative to the position of the origin
 */
int CArduinoZStage::SetPositionUm(double pos)
{
   if (DADevice_ == 0)
      return ERR_NO_DA_DEVICE;

   double volt = ( (pos + originPos_) / (maxStagePos_ - minStagePos_)) * (maxStageVolt_ - minStageVolt_);
   if (volt > maxStageVolt_ || volt < minStageVolt_)
      return ERR_POS_OUT_OF_RANGE;

   pos_ = pos;
   return DADevice_->SetSignal(volt);
}

/*
 * Reports the current position of the stage in um relative to the origin
 */
int CArduinoZStage::GetPositionUm(double& pos)
{
   if (DADevice_ == 0)
      return ERR_NO_DA_DEVICE;

   double volt;
   int ret = DADevice_->GetSignal(volt);
   if (ret != DEVICE_OK) 
      // DA Device cannot read, set position from cache
      pos = pos_;
   else
      pos = volt/(maxStageVolt_ - minStageVolt_) * (maxStagePos_ - minStagePos_) + originPos_;

   return DEVICE_OK;
}

/*
 * Sets a voltage (in mV) on the DA, relative to the minimum Stage position
 * The origin is NOT taken into account
 */
int CArduinoZStage::SetPositionSteps(long steps)
{
   if (DADevice_ == 0)
      return ERR_NO_DA_DEVICE;

   // Interpret steps to be mV
   double volt = minStageVolt_  + (steps / 1000.0);
   if (volt < maxStageVolt_)
      DADevice_->SetSignal(volt);
   else
      return ERR_VOLT_OUT_OF_RANGE;

   pos_ = volt/(maxStageVolt_ - minStageVolt_) * (maxStagePos_ - minStagePos_) + originPos_;

   return DEVICE_OK;
}

int CArduinoZStage::GetPositionSteps(long& steps)
{
   if (DADevice_ == 0)
      return ERR_NO_DA_DEVICE;

   double volt;
   int ret = DADevice_->GetSignal(volt);
   if (ret != DEVICE_OK)
      steps = (long) ((pos_ + originPos_)/(maxStagePos_ - minStagePos_) * (maxStageVolt_ - minStageVolt_) * 1000.0); 
   else
      steps = (long) ((volt - minStageVolt_) * 1000.0);

   return DEVICE_OK;
}

/*
 * Sets the origin (relative position 0) to the current absolute position
 */
int CArduinoZStage::SetOrigin()
{
   if (DADevice_ == 0)
      return ERR_NO_DA_DEVICE;

   double volt;
   int ret = DADevice_->GetSignal(volt);
   if (ret != DEVICE_OK)
      return ret;

   // calculate absolute current position:
   originPos_ = volt/(maxStageVolt_ - minStageVolt_) * (maxStagePos_ - minStagePos_);

   if (originPos_ < minStagePos_ || originPos_ > maxStagePos_)
      return ERR_POS_OUT_OF_RANGE;

   return DEVICE_OK;
}

int CArduinoZStage::GetLimits(double& min, double& max)
{
   min = minStagePos_;
   max = maxStagePos_;
   return DEVICE_OK;
}

int CArduinoZStage::IsStageSequenceable(bool& isSequenceable) const 
{
   return DADevice_->IsDASequenceable(isSequenceable);
}

int CArduinoZStage::GetStageSequenceMaxLength(long& nrEvents) const  
{
   return DADevice_->GetDASequenceMaxLength(nrEvents);
}

int CArduinoZStage::StartStageSequence()  
{
   return DADevice_->StartDASequence();
}

int CArduinoZStage::StopStageSequence()  
{
   return DADevice_->StopDASequence();
}

int CArduinoZStage::ClearStageSequence() 
{
   return DADevice_->ClearDASequence();
}

int CArduinoZStage::AddToStageSequence(double position) 
{
   double voltage;

      voltage = ( (position + originPos_) / (maxStagePos_ - minStagePos_)) * 
                     (maxStageVolt_ - minStageVolt_);
      if (voltage > maxStageVolt_)
         voltage = maxStageVolt_;
      else if (voltage < minStageVolt_)
         voltage = minStageVolt_;
   
   return DADevice_->AddToDASequence(voltage);
}

int CArduinoZStage::SendStageSequence()
{
   return DADevice_->SendDASequence();
}


///////////////////////////////////////
// Action Interface
//////////////////////////////////////
int CArduinoZStage::OnDADevice(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set(DADeviceName_.c_str());
   }
   else if (eAct == MM::AfterSet)
   {
      std::string DADeviceName;
      pProp->Get(DADeviceName);
      MM::SignalIO* DADevice = (MM::SignalIO*) GetDevice(DADeviceName.c_str());
      if (DADevice != 0) {
         DADevice_ = DADevice;
         DADeviceName_ = DADeviceName;
      } else
         return ERR_INVALID_DEVICE_NAME;
      if (initialized_)
         DADevice_->GetLimits(minDAVolt_, maxDAVolt_);
   }
   return DEVICE_OK;
}
int CArduinoZStage::OnPosition(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      double pos;
      int ret = GetPositionUm(pos);
      if (ret != DEVICE_OK)
         return ret;
      pProp->Set(pos);
   }
   else if (eAct == MM::AfterSet)
   {
      double pos;
      pProp->Get(pos);
      return SetPositionUm(pos);
   }
   return DEVICE_OK;
}
int CArduinoZStage::OnStageMinVolt(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set(minStageVolt_);
   }
   else if (eAct == MM::AfterSet)
   {
      double minStageVolt;
      pProp->Get(minStageVolt);
      if (minStageVolt >= minDAVolt_ && minStageVolt < maxDAVolt_)
         minStageVolt_ = minStageVolt;
      else
         return ERR_VOLT_OUT_OF_RANGE;
   }
   return DEVICE_OK;
}

int CArduinoZStage::OnStageMaxVolt(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set(maxStageVolt_);
   }
   else if (eAct == MM::AfterSet)
   {
      double maxStageVolt;
      pProp->Get(maxStageVolt);
      if (maxStageVolt > minDAVolt_ && maxStageVolt <= maxDAVolt_)
         maxStageVolt_ = maxStageVolt;
      else
         return ERR_VOLT_OUT_OF_RANGE;
   }
   return DEVICE_OK;
}

int CArduinoZStage::OnStageMinPos(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set(minStagePos_);
   }
   else if (eAct == MM::AfterSet)
   {
      pProp->Get(minStagePos_);
   }
   return DEVICE_OK;
}

int CArduinoZStage::OnStageMaxPos(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      pProp->Set(maxStagePos_);
   }
   else if (eAct == MM::AfterSet)
   {
      pProp->Get(maxStagePos_);
   }
   return DEVICE_OK;
}