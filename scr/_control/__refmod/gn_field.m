%/////////////////////////////////////////////////
% Research MATLAB code : calculate the magnetic field norm gradient by n
% magnets
% Author: Janaka (856518198)
% Date  : 01/21/2026
%
% Description:
%   This MATLAB code calculate the magnetic field norm gradient of the 
%   magnetic field generared by n magnets in a given location and the given 
%   angles
%
% Inputs:
%   par         : parameters
%   r           : position vector (2x1 vector)
%   theta_v     : angle vector of magnets (nx1 vector)
% Outputs:
%   gn_field    : gradient field vector (2x1 vector)
%/////////////////////////////////////////////////

function gn_field=gn_field(par,r,theta_v)
    Jacobn = Hn(par,r,theta_v);
    gn_field = Jacobn * hn_field(par,r,theta_v);
end