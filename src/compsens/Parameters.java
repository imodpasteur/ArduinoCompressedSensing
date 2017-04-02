/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */
package compsens;

import mmcorej.CMMCore;

/** A simple class to store parameters and pass them to the GUI
 *
 * @author maxime
 */
class Parameters {
    String basisPath;
    String porte;
    double basis[][];
    int    basis_row;
    int    basis_col;
    CMMCore core_;
    boolean arduino_present;
    int     arduino_basis_id;

    Parameters() {
        this.arduino_present = false;
    }
}
