package compsens;

import java.io.UnsupportedEncodingException;
import java.security.NoSuchAlgorithmException;
import java.util.logging.Level;
import java.util.logging.Logger;

/*
  This class contains most of the tools in order to communicate with the Arduino. 
  Note that it should rely on the MMCore driver to interact with the Arduino.
  Although if this turns out to be too complicated, we might communicate directly with the device
  (if possible)
 */


// ==== Imports

// ==== Main class
public class ArduinoLibs {
    /* 
       Class that contain functions to deal with Arduino stuff
     */
    BasisTools basistools = new BasisTools(); // Load CSV basis

    /*
    * Make sure that the Arduino is connected. This checks for the following
    * - Arduino Hub is present
    * - Arduino Hub is ready for CS mode
    */
    public String arduinoConnect(mmcorej.CMMCore core) {
        // Declarations
        int arduino_present = 0;
        String ret = "fail";
        
        // See if the Arduino device has been loaded
        mmcorej.StrVector devices = core.getLoadedDevices();        
        for (int i=0; i<devices.size(); i++){
            if ("Arduino-Hub".equals(devices.get(i))) {
                arduino_present = 1;
                System.out.println("Initialization of the device successful");
            }
        }

        if (arduino_present == 0) {
            ret = "Arduino-Hub not loaded. Aborting";
            return ret;
        }
            
        // See if it accepts CS mode
         try {   
            System.out.println("CS enabled: " + core.getProperty("Arduino-Hub", "CSEnabled"));
            if (core.getProperty("Arduino-Hub", "CSEnabled").equals("true")) {
                ret = "ok";
            }
        } catch (Exception ex) {
            Logger.getLogger(ArduinoLibs.class.getName()).log(Level.SEVERE, null, ex);
            ret = "fail";
        }
        return ret;
    }    
    
    /** 
    * Get the basisId property
     * @param core
     * @return
     * @throws Exception
     */
    public String arduinoBasisId(mmcorej.CMMCore core) throws Exception {
        System.out.println("CS enabled: " + core.getProperty("Arduino-Hub", "CSBasisId"));
        return core.getProperty("Arduino-Hub", "CSBasisId");
    }

    
    /**
     * Function takes a csv file (a matrix) as an input and returns a string 
     *   ready to be written to a .ino file
     * @param basis
     * @return
     */
    public String csvToIno(double basis[][]) {
        // Declarations
        String vec;
        int hash;
        
        // Hash
        hash = basistools.hashBasis(basis);
        vec = "#include <avr/pgmspace.h>\n";
        vec += "char basis_id[] = \"" + hash + "\";\n";
        vec += "int b_nelements; // Basis size, will be populated at runtime\n" +
                "int b_nvalues;\n" +
                "\n" +
                "// The measurement matrix do not fit in the RAM of the Arduino. However, it fits in SRAM (non-volatile).\n" +
                "// However, usually the Arduino variables are loaded into RAM, which in this case would cause a \n" +
                "// neat buffer overflow. \n" +
                "// Hence we use PROGMEM to dynamically read from the RAM (http://arduino.cc/en/Reference/PROGMEM )\n" +
                "// In terms of speed, PROGMEM is slower than direct RAM access. According to: http://forum.arduino.cc/index.php?topic=134782.0\n" +
                "// it takes three clock cycles to run instead of two, a time difference of ~62 ns. Acceptable.\n" +
                "// Note that PROGMEM seems not to support float types.\n" +
                "// Furthermore, the values are scaled so that there is no computation to perform before writing to the analog output.\n";
        
        vec += "const PROGMEM int16_t basis["+basis.length+"]["+basis[0].length+"] = {{";
        for (int j=0; j<basis.length; j++){
            for (int i=0; i<basis[0].length; i++){
                vec += basis[j][i];
                if (i+1 != basis[0].length){
                    vec += ',';
                }
            }
            if (j+1 != basis.length) {
                vec = vec + "},\n{";
            }
        }
        vec += "}};\n";
        vec += "// Compute the size of the basis\n\n" +
                "int init_basis(int &b_nelements, int &b_nvalues) {\n" +
                "   b_nvalues = sizeof(basis[0])/sizeof(basis[0][0]);\n" +
                "   b_nelements = sizeof(basis)/b_nvalues/sizeof(basis[0][0]);\n" +
                "  return 1;\n" +
                "}\n\n";
        
	System.out.println("Converting to CSV. Hash value is: " + hash);
	return(vec);
    }
}
