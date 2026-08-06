F_s = 1000.0;
T_s = 1.0 / F_s;
fprintf('Sampling time: %f\n', T_s);
%
% design a 2. order high pass filter with fc = 0.4
% B = numerator coefficients
% A = denominator coefficients 
% ------------------------------------------------
fnyq = F_s/2; 
[B,A] = butter(2, 0.4/fnyq, "high"); 

% ----------------------------------------------
H_h = tf(B, A, T_s);
fprintf('High pass filter coeffcients:\n');
for k=1:3
    fprintf('    IIR_Filt.num[%d] = %.15g;\n', k-1, B(k));
end
fprintf('\n');
for k=1:3
    fprintf('    IIR_Filt.den[%d] = %.15g;\n', k-1, A(k));
end
fprintf('\n');
Ghp = tf(B, A, T_s);
bode(Ghp); grid
fprintf('Press return to continue with low pass filter... '); pause
fprintf('\n');

%
% design a 8. order low pass filter with fc = 40
% B = numerator coefficients
% A = denominator coefficients 
% ----------------------------------------------
[B, A] = butter(8, 40/fnyq, "low"); 
% ----------------------------------------------
H_l = tf(B, A, T_s);
fprintf('Low pass filter coeffcients:\n');
for k=1:9
    fprintf('    IIR_Filt.num[%d] = %.15g;\n', k-1, B(k));
end
fprintf('\n');
for k=1:9
    fprintf('    IIR_Filt.den[%d] = %.15g;\n', k-1, A(k));
end
fprintf('\n');
Glp = tf(B, A, T_s);
bode(Glp); grid

fprintf('Press return to display step response... '); pause
fprintf('\n');
step(H_h*H_l); grid
disp('That`s all, folks.')
