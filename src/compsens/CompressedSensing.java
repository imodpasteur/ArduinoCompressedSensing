/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package compsens;

// ==== Imports
import org.micromanager.api.ScriptInterface;
import java.util.Properties;
/**
 * A class to implement compressed sensing in Micro Manager
 * @author maxime
 */
public class CompressedSensing implements org.micromanager.api.MMPlugin {   
    /** The menu name is stored in a static string, so Micro-Manager
	*  can obtain it without instantiating the plugin */
    public static String menuName = "Compressed Sensing!";	
	
    // ==== Declarations
    ScriptInterface app_; // to be populated by MM object
    mmcorej.CMMCore core_; // Get ready to handle the core_ (make sure it doesn't melt)
    
    Greeter greeter; // Say Hello
    ConfigManager config; // Store parameters
    BasisTools basistools; // Load CSV basis
    Properties prop; // Initialize empty properties dict
    PlotTools plotting; // Plotting with ImageJ
    final Parameters params = new Parameters();; // Simple communication class
   
    /** The main app calls this method to remove the module window */
    @Override
    public void dispose() {
        System.out.println("Closing CS");
    }
   
    /**
     * The main app passes its ScriptInterface to the module. This
     * method is typically called after the module is instantiated.
     * @param app - ScriptInterface implementation
    */
    @Override
    public void setApp(ScriptInterface app) {
        System.out.println("Instantiate CS");
        
        // ==== Initialize stuff
        greeter = new Greeter();
        config = new ConfigManager();
        basistools = new BasisTools();
        prop = new Properties();
        plotting = new PlotTools();
        boolean writeNewConfig = false;
        
        app_ = app; // Capture the object
        core_ = app_.getMMCore(); // Actually get the core.
        params.core_ = core_; // Register the core to be passed to the GUI
        
        System.out.println(greeter.sayHello());
        System.out.println("Working Directory = " + System.getProperty("user.dir"));

	// ==== Paths
	String configPath = "/home/maxime/zimmerdarzacq/11_micromanager/MM-CompressedSensing/data/config.properties";
        
	// ==== Read or write the configuration file
	if (writeNewConfig) {
	    config.writeConfig(configPath);
	    System.out.println("Written properties at " + configPath);
	    System.out.println(greeter.sayGoodbye());
	    System.exit(0);
	} else {
	    prop = config.readConfig(configPath);
	    System.out.println("Loaded properties from " + configPath);
	}
    }
   
    /** Open the module window  */
    @Override
    public void show() {
        // ==== Read basis file
	String basisPath = prop.getProperty("basis");
        double basis[][] = basistools.readBasis(basisPath);
	System.out.println("Loaded a measurement matrix with " + basis.length + " lines and " + basis[0].length + " columns from the file: " + basisPath);
        
        // ==== Create a population passing class
        params.basisPath = basisPath;
        params.basis = basis;
        params.basis_row = basis.length;
        params.basis_col = basis[0].length;

	// ==== Show the main GUI
        java.awt.EventQueue.invokeLater(new Runnable() {
            @Override
            public void run() {
                GraphicalPanel gui = new GraphicalPanel();
                gui.params = params; // pass the meaningful parameters
                gui.setVisible(true);
            }
        });
    }
   
   /**
    * The main app calls this method when hardware settings change.
    * This call signals to the module that it needs to update whatever
    * information it needs from the MMCore.
    */
   public void configurationChanged() {
        System.out.println("Do nothing");
    }
   
   /**
    * Returns a very short (few words) description of the module.
     * @return 
    */
   @Override
   public String getDescription() {
       return "This is a short description of the module";
    }
   
   /**
    * Returns verbose information about the module.
    * This may even include a short help instructions.
     * @return 
    */
   @Override
   public String getInfo() {
       return "This is a long description of the module";
    }
   
   /**
    * Returns version string for the module.
    * There is no specific required format for the version
     * @return 
    */
   @Override
   public String getVersion() {
        return "v1.0";
    }
   
   /**
    * Returns copyright information
     * @return 
    */
   @Override
   public String getCopyright() {
       return "GPLv3+";
    }
   
}