 //////////////////////////////////////////////////////////////////////////////
// FILE:          Arduino.h
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   Adapter for Arduino board
//                Needs accompanying firmware to be installed on the board
// COPYRIGHT:     University of California, San Francisco, 2008
// LICENSE:       LGPL
//
// AUTHOR:        Nico Stuurman, nico@cmp.ucsf.edu, 11/09/2008
//                automatic device detection by Karl Hoover
//
//

#ifndef _Arduino_H_
#define _Arduino_H_

#include "../../MMDevice/MMDevice.h"
#include "../../MMDevice/DeviceBase.h"
#include <string>
#include <sstream>
#include <map>

//////////////////////////////////////////////////////////////////////////////
// Error codes
//
#define ERR_UNKNOWN_POSITION 101
#define ERR_INITIALIZE_FAILED 102
#define ERR_WRITE_FAILED 103
#define ERR_CLOSE_FAILED 104
#define ERR_BOARD_NOT_FOUND 105
#define ERR_PORT_OPEN_FAILED 106
#define ERR_COMMUNICATION 107
#define ERR_NO_PORT_SET 108
#define ERR_VERSION_MISMATCH 109


//////////////////////////////////////////////////////////////////////////////
// Error codes for the Z-Stage module (copied from Utilities.cpp > DAZStage)
//
#define ERR_INVALID_DEVICE_NAME            10001
#define ERR_NO_DA_DEVICE                   10002
#define ERR_VOLT_OUT_OF_RANGE              10003
#define ERR_POS_OUT_OF_RANGE               10004
#define ERR_NO_DA_DEVICE_FOUND             10005
#define ERR_NO_STATE_DEVICE                10006
#define ERR_NO_STATE_DEVICE_FOUND          10007
#define ERR_NO_AUTOFOCUS_DEVICE            10008
#define ERR_NO_AUTOFOCUS_DEVICE_FOUND      10009
#define ERR_NO_AUTOFOCUS_DEVICE_FOUND      10009
#define ERR_NO_PHYSICAL_CAMERA             10010
#define ERR_NO_EQUAL_SIZE                  10011
#define ERR_AUTOFOCUS_NOT_SUPPORTED        10012
#define ERR_NO_PHYSICAL_STAGE              10013
#define ERR_TIMEOUT                        10021


class ArduinoInputMonitorThread;

class CArduinoHub : public HubBase<CArduinoHub>  
{
public:
   CArduinoHub();
   ~CArduinoHub();

   int Initialize();
   int Shutdown();
   void GetName(char* pszName) const;
   bool Busy();

   bool SupportsDeviceDetection(void);
   MM::DeviceDetectionStatus DetectDevice(void);
   int DetectInstalledDevices();

   // property handlers
   int OnPort(MM::PropertyBase* pPropt, MM::ActionType eAct);
   int OnLogic(MM::PropertyBase* pPropt, MM::ActionType eAct);
   int OnVersion(MM::PropertyBase* pPropt, MM::ActionType eAct);
   int OnCSOnOff(MM::PropertyBase* pProp, MM::ActionType pAct); // CS mode
   int OnExposureChanged(MM::PropertyBase* pProp, MM::ActionType eAct);
   //int ReadNBytes(CArduinoHub* hub, unsigned int n, unsigned char* answer);

   // custom interface for child devices
   bool IsPortAvailable() {return portAvailable_;}
   bool IsLogicInverted() {return invertedLogic_;}
   bool IsTimedOutputActive() {return timedOutputActive_;}
   void SetTimedOutput(bool active) {timedOutputActive_ = active;}

   int PurgeComPortH() {return PurgeComPort(port_.c_str());}
   int WriteToComPortH(const unsigned char* command, unsigned len) {return WriteToComPort(port_.c_str(), command, len);}
   int ReadFromComPortH(unsigned char* answer, unsigned maxLen, unsigned long& bytesRead)
   {
      return ReadFromComPort(port_.c_str(), answer, maxLen, bytesRead);
   }
   static MMThreadLock& GetLock() {return lock_;}
   void SetShutterState(unsigned state) {shutterState_ = state;}
   void SetSwitchState(unsigned state) {switchState_ = state;}
   unsigned GetShutterState() {return shutterState_;}
   unsigned GetSwitchState() {return switchState_;}

private:
   int GetControllerVersion(int&);
   int GetCSMode(int&); // Check if the Arduino firmware supports CS
   int GetCSBasisId(int& basis_id); // Get the hashCode of the basis loaded on the Arduino
   std::string port_;
   bool initialized_;
   bool portAvailable_;
   bool invertedLogic_;
   bool timedOutputActive_;
   int version_;
   static MMThreadLock lock_;
   unsigned switchState_;
   unsigned shutterState_;
   int cs_firmware_; // 1 if the Arduino firmware is compatible with CS
   int cs_basis_id_; // The hashCode of the basis on the Arduino
};

class CArduinoShutter : public CShutterBase<CArduinoShutter>  
{
public:
   CArduinoShutter();
   ~CArduinoShutter();
  
   // MMDevice API
   // ------------
   int Initialize();
   int Shutdown();
  
   void GetName(char* pszName) const;
   bool Busy();
   
   // Shutter API
   int SetOpen(bool open = true);
   int GetOpen(bool& open);
   int Fire(double deltaT);

   // action interface
   // ----------------
   int OnOnOff(MM::PropertyBase* pProp, MM::ActionType eAct);

private:
   int WriteToPort(long lnValue);
   MM::MMTime changedTime_;
   bool initialized_;
   std::string name_;
};

class CArduinoSwitch : public CStateDeviceBase<CArduinoSwitch>  
{
public:
   CArduinoSwitch();
   ~CArduinoSwitch();
  
   // MMDevice API
   // ------------
   int Initialize();
   int Shutdown();
  
   void GetName(char* pszName) const;
   bool Busy() {return busy_;}
   
   unsigned long GetNumberOfPositions()const {return numPos_;}

   // action interface
   // ----------------
   int OnState(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnDelay(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnRepeatTimedPattern(MM::PropertyBase* pProp, MM::ActionType eAct);
   /*
   int OnSetPattern(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnGetPattern(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnPatternsUsed(MM::PropertyBase* pProp, MM::ActionType eAct);
   */
   int OnSkipTriggers(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnStartTrigger(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnStartTimedOutput(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnBlanking(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnBlankingTriggerDirection(MM::PropertyBase* pProp, MM::ActionType eAct);

   int OnSequence(MM::PropertyBase* pProp, MM::ActionType eAct);

private:
   static const unsigned int NUMPATTERNS = 12;

   int OpenPort(const char* pszName, long lnValue);
   int WriteToPort(long lnValue);
   int ClosePort();
   int LoadSequence(unsigned size, unsigned char* seq);

   unsigned pattern_[NUMPATTERNS];
   unsigned delay_[NUMPATTERNS];
   int nrPatternsUsed_;
   unsigned currentDelay_;
   bool sequenceOn_;
   bool blanking_;
   bool initialized_;
   long numPos_;
   bool busy_;
};

class CArduinoDA : public CSignalIOBase<CArduinoDA>  
{
public:
   CArduinoDA(int channel);
   ~CArduinoDA();
  
   // MMDevice API
   // ------------
   int Initialize();
   int Shutdown();
  
   void GetName(char* pszName) const;
   bool Busy() {return busy_;}

   // DA API
   int SetGateOpen(bool open);
   int GetGateOpen(bool& open) {open = gateOpen_; return DEVICE_OK;};
   int SetSignal(double volts);
   int GetSignal(double& volts) {volts_ = volts; return DEVICE_UNSUPPORTED_COMMAND;}     
   int GetLimits(double& minVolts, double& maxVolts) {minVolts = minV_; maxVolts = maxV_; return DEVICE_OK;}
   
   int IsDASequenceable(bool& isSequenceable) const {isSequenceable = false; return DEVICE_OK;}

   // action interface
   // ----------------
   int OnVolts(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnMaxVolt(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnChannel(MM::PropertyBase* pProp, MM::ActionType eAct);

private:
   int WriteToPort(unsigned long lnValue);
   int WriteSignal(double volts);

   bool initialized_;
   bool busy_;
   double minV_;
   double maxV_;
   double volts_;
   double gatedVolts_;
   unsigned channel_;
   unsigned maxChannel_;
   bool gateOpen_;
   std::string name_;
};

class CArduinoInput : public CGenericBase<CArduinoInput>  
{
public:
   CArduinoInput();
   ~CArduinoInput();

   int Initialize();
   int Shutdown();
   void GetName(char* pszName) const;
   bool Busy();

   int OnDigitalInput(MM::PropertyBase* pPropt, MM::ActionType eAct);
   int OnAnalogInput(MM::PropertyBase* pProp, MM::ActionType eAct, long channel);

   int GetDigitalInput(long* state);
   int ReportStateChange(long newState);

private:
   int ReadNBytes(CArduinoHub* h, unsigned int n, unsigned char* answer);
   int SetPullUp(int pin, int state);

   MMThreadLock lock_;
   ArduinoInputMonitorThread* mThread_;
   char pins_[MM::MaxStrLength];
   char pullUp_[MM::MaxStrLength];
   int pin_;
   bool initialized_;
   std::string name_;
};

class ArduinoInputMonitorThread : public MMDeviceThreadBase
{
   public:
      ArduinoInputMonitorThread(CArduinoInput& aInput);
     ~ArduinoInputMonitorThread();
      int svc();
      int open (void*) { return 0;}
      int close(unsigned long) {return 0;}

      void Start();
      void Stop() {stop_ = true;}
      ArduinoInputMonitorThread & operator=( const ArduinoInputMonitorThread & ) 
      {
         return *this;
      }


   private:
      long state_;
      CArduinoInput& aInput_;
      bool stop_;
};


/**
 * Allows a DA device to act like a Drive (better hook it up to a drive!)
 */
class CArduinoZStage : public CStageBase<CArduinoZStage>
{
public:
   CArduinoZStage();
   ~CArduinoZStage();
  
   // Device API
   // ----------
   int Initialize();
   int Shutdown();
  
   void GetName(char* pszName) const;
   bool Busy();

   // Stage API
   // ---------
  int SetPositionUm(double pos);
  int GetPositionUm(double& pos);
  int SetPositionSteps(long steps);
  int GetPositionSteps(long& steps);
  int SetOrigin();
  int GetLimits(double& min, double& max);

  bool IsContinuousFocusDrive() const {return false;}


   // action interface
   // ----------------
   int OnDADevice(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnStageMinVolt(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnStageMaxVolt(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnStageMinPos(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnStageMaxPos(MM::PropertyBase* pProp, MM::ActionType eAct);
   int OnPosition(MM::PropertyBase* pProp, MM::ActionType eAct);

   // Sequence functions
   int IsStageSequenceable(bool& isSequenceable) const;
   int GetStageSequenceMaxLength(long& nrEvents) const;
   int StartStageSequence();
   int StopStageSequence();
   int ClearStageSequence();
   int AddToStageSequence(double position);
   int SendStageSequence();

private:
   std::vector<std::string> availableDAs_;
   std::string DADeviceName_;
   MM::SignalIO* DADevice_;
   bool initialized_;
   double minDAVolt_;
   double maxDAVolt_;
   double minStageVolt_;
   double maxStageVolt_;
   double minStagePos_;
   double maxStagePos_;
   double pos_;
   double originPos_;
};


//////////// CS stuff ///////////////
void testFunc();


#endif //_Arduino_H_
