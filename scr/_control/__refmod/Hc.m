%/////////////////////////////////////////////////
% Research MATLAB code : calculate the Jacobean of the magnetic field
% Author: Janaka (856518198)
% Date  : 09/05/2023
%
% Description:
%   This MATLAB code calculate the Jacobean of the magnetic field w.r.t a
%   given position vector "r"
%
% Inputs:
%   par         : parameters
%   r           : position vector (2x1 vector)
% Outputs:
%   Hc          : Jacobean matrix (2x2 matrix)
%
% Notes: Case (1) (simple model) is to be verified
%/////////////////////////////////////////////////

function H_c = Hc(par,r)
    model = par.model_sel;

    switch model
        case 0 % complex model
            rho = par.rho;
            ak = par.ak_comp;
            r_sub_roh = [r' 0]' - rho;
            norm_rsr = vecnorm(r_sub_roh);
            wp1 = (1./(norm_rsr.^3)) * ak;
            jp1 = wp1 * eye(3);
            wp2 = ak ./ (norm_rsr.^5)';
            jp2 = 3*(r_sub_roh * diag(wp2) * r_sub_roh');
            Jacob = jp1 - jp2;
            H_c = Jacob(1:2,1:2);

        case 1 % simple model
            ak = par.ak_dipole;
            m = par.m;
            norm_r = norm(r);

            j11 = ((2*m(1)*r(1) + m(2)*r(2))/norm_r^5) - (5*(m(1)*r(1)^2 + m(2)*r(1)*r(2))*r(1)/norm_r^7 + (3*m(1)*r(1)/norm_r^5));
            j12 = ((m(2)*r(1)/norm_r^5) - (5*(m(1)*r(1)^2 + m(2)*r(1)*r(2))*r(2)/norm_r^7) + (3*m(1)*r(2)/norm_r^5));
            j21 = ((m(1)*r(2)/norm_r^5) - (5*(m(1)*r(1)*r(2) + m(2)*r(2)^2)*r(1)/norm_r^7) + (3*m(2)*r(1)/norm_r^5));
            j22 = (((m(1)*r(1) + 2*m(2)*r(2))/norm_r^5) - (5*(m(1)*r(1)*r(2) + m(2)*r(2)^2)*r(2)/norm_r^7) + (3*m(2)*r(2)/norm_r^5));
            H_c = ak.*[j11 j12;j21 j22];

        otherwise % default (complex)
            rho = par.rho;
            ak = par.ak_comp;
            r_sub_roh = [r' 0]' - rho;
            norm_rsr = vecnorm(r_sub_roh);
            wp1 = (1./(norm_rsr.^3)) * ak;
            jp1 = wp1 * eye(3);
            wp2 = ak ./ (norm_rsr.^5)';
            jp2 = 3*(r_sub_roh * diag(wp2) * r_sub_roh');
            Jacob = jp1 - jp2;
            H_c = Jacob(1:2,1:2);
    end
end