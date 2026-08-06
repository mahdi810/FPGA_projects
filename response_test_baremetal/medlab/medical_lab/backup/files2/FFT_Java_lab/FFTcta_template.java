package plotd;

// FFTcta: Cooley-Tukey FFT calculation

public class FFTcta {

    private final int MAX_POINTS = 1024, MAX_PTS2 = MAX_POINTS / 2;
    private double si_re[] = new double[MAX_POINTS];
    private double si_im[] = new double[MAX_POINTS];
    private double so_re[] = new double[MAX_POINTS];
    private double so_im[] = new double[MAX_POINTS];
    private double WNn_re[] = new double[MAX_PTS2];
    private double WNn_im[] = new double[MAX_PTS2];
    private int NN, NN2, NNlog2;
    // sample data
    private final double T_s = 1.0 / 8.0E3;
    private final double f1 = 1000.0, f2 = 2000.0;
    private int N_SAMPLES = 0;
    private double xx[] = new double[MAX_POINTS];



    int FftGetLength() {
    	return N_SAMPLES;
    }

    double FftGetXk(int k) {
    	return xx[k];
    }

    double FftGetReal(int k) {
    	return so_re[k];
    }

    double FftGetImag(int k) {
    	return so_im[k];
    }

    boolean FFTrun(int N_Samples)
    {
    	boolean rc_success;

    	N_SAMPLES = N_Samples;
    	rc_success = false;
    	return rc_success;
    }

    public FFTcta() {
    	System.out.println("--- FFT Cooley-Tukey Algorithm ---");
    }
	
}
