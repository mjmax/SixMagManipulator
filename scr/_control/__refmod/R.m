%/////////////////////////////////////////////////
% Research MATLAB code : calculate the rotational matrix
% Author: Janaka (856518198)
% Date  : 08/31/2023
%
% Description:
%   This MATLAB code calculate rotational matrix from local coordinate system
%   to global coordinate system
%
% Inputs:
%   phi         : rotational angle
% Outputs:
%   rot_mat     : rotational matrix (2x2 matrix)
%/////////////////////////////////////////////////

function rot_mat= R(phi)
    %rot_mat = [cosd(phi) sind(phi); -sind(phi) cosd(phi)];  
    rot_mat = [cos(phi) sin(phi); -sin(phi) cos(phi)]; 
end