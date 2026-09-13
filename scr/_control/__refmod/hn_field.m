%/////////////////////////////////////////////////
% Research MATLAB code : calculate the magnetic field inside the working bed
% Author: Janaka (856518198)
% Date  : 01/21/2026
%
% Description:
%   This MATLAB code calculate the magnetic field due to the six magnets in a
%   given position inside the working bed base on the dipole/complex model 
%   summation model
%
% Inputs:
%   par         : parameters
%   r           : position vector (2x1 vector)
%   theta_v     : angle vector of magnets (nx1 vector)
% Outputs:
%   h_field     : magnetic field vector (2x1 vector)
%/////////////////////////////////////////////////

function hn_f=hn_field(par,r,theta_v)
    phi_rad_k = par.phi_rad;
    rho_k = par.rho_k_rad;
    diff_pos = (r - rho_k');

    hc_global = zeros(2, par.n); % final
    
    for i = 1:par.n
        ang = theta_v(i) + phi_rad_k(i);
        Ri  = R(ang);                    % compute once (2x2)
    
        % pk(:,i) = [Ri*diff_pos(:,i); 0]
        pi = Ri * diff_pos(:,i);         % 2x1
        % pk(3,i) is already 0
    
        % h_c returns 3x1 (assumed), but we only keep first two components
        hci = h_c(par, [pi;0]);         % 3x1
        hci2 = hci(1:2);              % drop z component immediately
    
        % global transform: R' * hc(:,i)
        hc_global(:,i) = Ri.' * hci2;
    end

    h_f_org = sum(hc_global,2);
    hn_f = sqrt(2*par.kg)*h_f_org;
end