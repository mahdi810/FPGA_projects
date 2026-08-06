function phi_xx = acf_m(x, n_acf)
% phi_xx = akf_m(x, n_acf)
%     auto-correlation function of x,
%     n_acf is the korrelation window size.

[r, c] = size(x);
if c~=1
    fprintf("r: %d\n", r)
    error('*** x is not a column vector.');
end
phi_xx = zeros(n_acf, 1);
% acf calculation starts here...
xt = x(1:n_acf).'; 
for tau = 1:n_acf 
    phi_xx(tau) = xt * x(tau:tau+n_acf-1); 
end 


