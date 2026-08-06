% loadatm : script file for acquiring data from PysioBanks
%           Automatic Teller Machine (ATM)

% usage: loadatm
%   The data files are asked during execution
%   Files xxxx.mat and xxxx.info must exist in the current
%   directory.
%
%   This script will create the following variables
%   in the current workspace. Variables with the
%   same name are destroyed.

% This script uses code from plotATM.m by O. Abdala, 2009

files_found = 0;
while files_found == 0
    dataName = input(' * enter data name (no extension): ', 's');
    matName = strcat(dataName, '.mat');
    infoName = strcat(dataName, '.info');
    if ~exist(matName, 'file')
        fprintf('*** %s does not exist.\n', matName);
    elseif ~exist(infoName, 'file')
        fprintf('*** %s does not exist.\n', infoName);
        fprintf('Please select the corresponding info file:\n');
        infofiles = dir('*.info');
        for k=1:length(infofiles)
            fprintf('%20s [%d]\n', infofiles(k).name, k);
        end
        k_select = input('==> Info file number: ');
        if length(k_select)==0
            k_select = -1;
        end
        if (k_select > 0) && (k_select <= length(infofiles))
            infoName = infofiles(k_select).name;
            files_found = 1;
        end
    else
        files_found = 1;
        fprintf(' o files found, laoding data...\n');
    end
end
load(matName);      % should read val
val = val';         % data should be organized ion columns
val_f = val;
[ndata, nsigs] = size(val);
fid = fopen(infoName, 'rt');
fgetl(fid);
fgetl(fid);
fgetl(fid);
[freqint] = sscanf(fgetl(fid), ...
    'Sampling frequency: %f Hz  Sampling interval: %f sec');
interval = freqint(2);
f_sample = 1.0 / interval;
fprintf(' o sampling frequency %.3f\n', f_sample);
fgetl(fid);
row = zeros(1, nsigs);
signal = cell(1, nsigs);
gain = zeros(1, nsigs);
base = zeros(1, nsigs);
units = cell(1, nsigs);
for k=1:nsigs
    [row(k), signal(k), gain(k), base(k), units(k)] ...
        = strread(fgetl(fid),'%d%s%f%f%s','delimiter','\t');
end
fclose(fid);
val(val==-32768) = NaN;
for k=1:nsigs
    val_f(:, k) = (val(:, k) - base(k)) / gain(k);
end
x = (1:ndata) * interval;
x = x';
% ask for plot curve
fprintf(' o %d signals found\n', nsigs);
s_select = input('==> Select curve number [or nothing for all curves]: ', 's');
if length(s_select)==0
    plot(x, val_f);
    labels = cell(nsigs, 1);
    for k=1:nsigs
        labels{k}=strcat(signal{k},' (',units{k},')');
    end
    legend(labels);
    xlabel('Time (sec)');
else
    k = str2double(s_select);
    if (k < 0) || (k > nsigs)
        fprintf(' *** signal out of range - Goodbye.\n');
    else
        plot(x, val_f(:,k));
        legend(strcat(signal{k},' (',units{k},')'));
        xlabel('Time (sec)');
    end
end
fprintf('That`s all, folks.\n');
