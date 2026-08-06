% heartbeatdet

fprintf("Load ECG data into the vector ""uu"".\n")
uu = val_f(:,9);
% uu = val_f(:,3);
% uu = val_f(:,11);
fs = 1000.0;		% sampling frequency
% bandpass filter [0.4 ... 30.0]

NN = length(uu);
k = (0:NN-1).';
figure(1);
% stairs(k, [uu uf]); title('raw and filtered ECG data'); grid
n_acf = 4000;
phi = acf_m(uf(1000:end), n_acf);
for k=1:n_acf
    if phi(k) < 0.0
        phi(k) = -sqrt(abs(phi(k))/n_acf);
    else
        phi(k) = sqrt(phi(k)/n_acf);
    end
end
tau = (0:n_acf-1).';
figure(2);
% stairs(tau, phi); title('Autocorrelation function PHIxx(tau)'); grid
whos tau phi
% Detect period
phi_max = phi(1);
phi_thresh = 0.8 * phi_max;
fprintf("phi_max: %8.5f   |  phi_thresh: %8.5f\n", phi_max, phi_thresh);

k = 2;
% k_left and k_right has to be determined...
% The following values are not correct of course.
% ---------------------------------------------------------------------
k_left = 5;
k_right = 10;
% ---------------------------------------------------------------------

fprintf("k_right: %d\n", k_right);
hb_period = 0.5 * (k_right + k_left);
fprintf("hb_period: %8.4f ms\n", hb_period);
hb_rate = 60000.0 / hb_period;
fprintf("+------------------------+\n");
fprintf("| hb_rate:  %8.4f bpm |\n", hb_rate);
fprintf("+------------------------+\n");

disp('*** That`s all, folks. ***')
