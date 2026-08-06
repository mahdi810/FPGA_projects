function hk = ifft_shift(Hm)

N = length(Hm);
N2 = N / 2;
xx = ifft([Hm(N2:N); Hm(1:N2-1)]);
% figure(2)
% stem([0:N-1]' ,xx);
% stem(xx);
hk = [xx(N2+2:N); xx(1:N2+1)];
