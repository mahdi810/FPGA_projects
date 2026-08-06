function [mag, ws] = linfreqresp(G, wmax)
% Frequency response for linear w-axis
% Format:  mag = linfreqresp(G, wmax)

% (c) 2011 Univ. Bremerhaven, ESD

if nargin < 1 || nargin > 2
    error('Format:  mag = linfreqresp(G, {wmax})');
end
if nargin == 1
    [num, den, T_s] = tfdata(G);
    wmax = 0.5 / T_s;
end
w = linspace(0, wmax, 501);
w = w';
xmag = freqresp(G, w, 'Hz');
xmag = abs(xmag(:));
xws = [-w(501:-1:2); w];
xmag = [xmag(501:-1:2); xmag];
if nargout == 0
    plot(xws, xmag);
    title('Frequency response on linear axis');
else
    mag = xmag;
    ws = xws;
end
