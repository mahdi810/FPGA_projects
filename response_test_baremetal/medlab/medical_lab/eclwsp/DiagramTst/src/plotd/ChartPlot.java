package plotd;

import java.awt.EventQueue;

import javax.swing.JFrame;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;
import javax.swing.JMenu;
import javax.swing.JPanel;
import java.awt.BorderLayout;
import java.awt.Color;

import javax.swing.JButton;
import java.awt.event.ActionListener;
import java.awt.event.ActionEvent;
import java.awt.FlowLayout;
import javax.swing.border.EtchedBorder;

import info.monitorenter.gui.chart.Chart2D;
import info.monitorenter.gui.chart.traces.Trace2DSimple;

public class ChartPlot {

	private JFrame frmDiagramTest;
	private JPanel Chart_panel;
    private Chart2D chart;
    static final int NTRACES = 2;
    private static Trace2DSimple trace_0;
    private static Trace2DSimple trace_1;
    private static int XCounter = 0;
    private FFTcta FftOBJ; 
    
	/**
	 * Launch the application.
	 */
	public static void main(String[] args) {
		EventQueue.invokeLater(new Runnable() {
			public void run() {
				try {
					ChartPlot window = new ChartPlot();
					window.frmDiagramTest.setVisible(true);
				} catch (Exception e) {
					e.printStackTrace();
				}
			}
		});
	}

	/**
	 * Create the application.
	 */
	public ChartPlot() {
		initialize();
		FftOBJ = new FFTcta(); 
	}

	/**
	 * Initialize the contents of the frame.
	 */
	private void initialize() {
		frmDiagramTest = new JFrame();
		frmDiagramTest.setTitle("Diagram Test");
		frmDiagramTest.setBounds(100, 100, 689, 494);
		frmDiagramTest.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
		
		JMenuBar menuBar = new JMenuBar();
		frmDiagramTest.setJMenuBar(menuBar);
		
		JMenu mnNewMenu = new JMenu("File");
		menuBar.add(mnNewMenu);
		
		JMenuItem mntmNewMenuItem = new JMenuItem("Help");
		mnNewMenu.add(mntmNewMenuItem);
		
		JMenuItem mntmNewMenuItem_1 = new JMenuItem("Exit");
		mntmNewMenuItem_1.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				System.exit(0);
				
			}
		});
		mnNewMenu.add(mntmNewMenuItem_1);
		
		JPanel panel = new JPanel();
		panel.setBorder(new EtchedBorder(EtchedBorder.LOWERED, null, null));
		FlowLayout flowLayout = (FlowLayout) panel.getLayout();
		flowLayout.setHgap(20);
		frmDiagramTest.getContentPane().add(panel, BorderLayout.SOUTH);
		
		JButton btnNewButton = new JButton("Clear");
		btnNewButton.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent arg0) {
				System.out.println("Clear");
				ClearChart();
			}
		});
		panel.add(btnNewButton);
		
		JButton btnNewButton_1 = new JButton("FFT-8");
		btnNewButton_1.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				FftOBJ.FFTrun(8);
				Plotfftresult();
			}
		});
		panel.add(btnNewButton_1);
		
		JButton btnNewButton_2 = new JButton("FFT-16");
		btnNewButton_2.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				FftOBJ.FFTrun(16);
				Plotfftresult();
			}
		});
		panel.add(btnNewButton_2);
		
		JButton btnNewButton_3 = new JButton("FFT-64");
		btnNewButton_3.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				FftOBJ.FFTrun(64);
				Plotfftresult();
			}
		});
		panel.add(btnNewButton_3);
		
		Chart_panel = new JPanel();
		Chart_panel.setBorder(new EtchedBorder(EtchedBorder.LOWERED, null, null));
		frmDiagramTest.getContentPane().add(Chart_panel, BorderLayout.CENTER);

		// add chart to Chart_panel
		CreateChart();
	}


	private void Plotfftresult()
	{ 	int NN; 
		double  re, im, mag;
	
		final int NPOINTS = 51;
		int  k;
		double x, x_l, xrad, y0, y1;
		
		NN = FftOBJ.FftGetLength(k); 
		for(k = 0; k< NN; k++)
		{
			re = FftOBJ.FftGetReal(k); 
			im = FftOBJ.FftGetImag(k); 
			mag = Math.hypot(re,  im);
			System.out.printf("k: %3d  re: %8.4f  im: %8.4f   mag: %8.4f \n", k, re, im, mag)
		}
		
		
		
		for(k = 0; k < NPOINTS  ; k++) {
			xrad = k * 2.0 * Math.PI / NPOINTS;
			y0 = 0.8 * Math.cos(xrad );
			y1 = 0.6 * Math.sin(xrad); 
			
			
			trace_0.addPoint(k + XCounter, y0); 
			trace_1.addPoint(k + XCounter, y1); 
			XCounter++; 
 
		}
		XCounter+= NPOINTS; 
	}

	static void ClearChart()
	{
		XCounter = 0;
		trace_0.removeAllPoints();
		trace_1.removeAllPoints();		
	}

	private void CreateChart()
	{
		chart = new Chart2D();
        trace_0 = new Trace2DSimple();
        chart.addTrace(trace_0);
        trace_1 = new Trace2DSimple();
        chart.addTrace(trace_1);
        trace_0.setColor(Color.blue);  trace_0.setName("Cosine");
        trace_1.setColor(Color.red);  trace_1.setName("Sine");
        Chart_panel.setLayout(new BorderLayout(0, 0));
        
        Chart_panel.add(chart);
        // chart_panel.setSize(100,200);
        chart.setVisible(true);
        Chart_panel.setVisible(true);
        Chart_panel.repaint();	
	}


}
