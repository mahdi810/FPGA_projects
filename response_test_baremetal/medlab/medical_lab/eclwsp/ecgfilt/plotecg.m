% plotecg

u = load('u.dat', '-ascii');
y = load('y.dat', '-ascii');
T_s = 1.0e-3;
k = (0:length(u)-1)';
t = k * T_s;
stairs(t, [u y]);
title('ECG raw data and filtered data');
grid
disp('Good-Bye.')
