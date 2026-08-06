% cplot: plot ascii lists from C code

disp('--- ploting u.dat and y.dat from C ---')
u = load('u_ecg.dat', '-ascii');
y = load('y.dat', '-ascii');
[ur, uc] = size(u);
[yr, yc] = size(y);
if uc~=1
    error('*** u is not a column vector.')
end
if yc~=1
    error('*** y is not a column vector.')
end
if ur~=yr
    error('*** u and y differ in size.')
end
k = T_s * (0:ur-1).';
stairs(k, [u y]); grid
title('u (input), y (output - from C)')
% step(H_h*H_l, 0.11); grid
