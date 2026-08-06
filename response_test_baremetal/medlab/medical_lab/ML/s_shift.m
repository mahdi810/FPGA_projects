function ssh = s_shift(N, n_shift)
% s_shift factors computation
%     ssh = s_shift(N, n_shift)
%           N = number of coefficients
%           n_shift = shift frequency
%                     n = N/2 => high pass

% K. Mueller: 08-DEC-2013

narginchk(2, 2);

ssh = zeros(N, 1);
k = (0:N-1)';
NN2 = fix(N / 2);
if rem(N, 2) == 0
    % even
    ssh = cos(2 * pi * (k - NN2 + 1) * n_shift / N);
else
    % odd
    ssh = cos(2 * pi * (k - NN2) * n_shift / N);
end
