%/////////////////////////////////////////////////
% Research MATLAB code : calculate the Jacobean of the magnetic field
% Author: Janaka (856518198)
% Date  : 05/07/2026
%
% Description:
%   This MATLAB code calculate the Jacobean of the magnetic field w.r.t a
%   given position vector "r"
%
% Inputs:
%   par         : parameters
%   r           : position vector (2x1 vector)
% Outputs:
%   dvecHc_dw   : matrix (4x2 matrix)
%
% Notes: Case (1) (simple model) is to be verified
%/////////////////////////////////////////////////

function dvecHc_dw = dvecHcdw(par,r)
    model = par.model_sel;
    vecI3 = [1 0 0 0 1 0 0 0 1]';
    E23 = [1 0 0;0 1 0];
    E32 = E23';
    switch model
        case 0 % complex model
            rho = par.rho;
            ak = par.ak_comp;
            w = [r' 0]' - rho;                         % 3 x N
            nr = vecnorm(w);                     % 1 x N
            wp1 = ak ./ (nr.^5).';               % N x 1
            wp2 = ak ./ (nr.^7).';               % N x 1
            vec_rrT = reshape(reshape(w,3,1,[]) .* reshape(w,1,3,[]), 9, []);
            jp1 = 5 * vec_rrT * (wp2 .* w.');    % 9 x 3
            jp2 = repmat(vecI3,1,size(w,2)) * (wp1 .* w.');  % 9 x 3
            v = w * wp1;                         % 3 x 1
            jp3 = kron(v,eye(3)) + kron(eye(3),v);            % 9 x 3
            dvecH3Dc_dw = 3*(jp1 - jp2 - jp3);
            dvecHc_dw = kron(E23,E23) * dvecH3Dc_dw * E32;

        case 1 % simple model
%             ak = par.ak_dipole;
%             m = par.m;
%             norm_r = norm(r);
% 
%             j11 = ((2*m(1)*r(1) + m(2)*r(2))/norm_r^5) - (5*(m(1)*r(1)^2 + m(2)*r(1)*r(2))*r(1)/norm_r^7 + (3*m(1)*r(1)/norm_r^5));
%             j12 = ((m(2)*r(1)/norm_r^5) - (5*(m(1)*r(1)^2 + m(2)*r(1)*r(2))*r(2)/norm_r^7) + (3*m(1)*r(2)/norm_r^5));
%             j21 = ((m(1)*r(2)/norm_r^5) - (5*(m(1)*r(1)*r(2) + m(2)*r(2)^2)*r(1)/norm_r^7) + (3*m(2)*r(1)/norm_r^5));
%             j22 = (((m(1)*r(1) + 2*m(2)*r(2))/norm_r^5) - (5*(m(1)*r(1)*r(2) + m(2)*r(2)^2)*r(2)/norm_r^7) + (3*m(2)*r(2)/norm_r^5));
%             H_c = ak.*[j11 j12;j21 j22];

            rho = par.rho;
            ak = par.ak_comp;
            r_sub_roh = [r' 0]' - rho;
            norm_rsr = vecnorm(r_sub_roh);
            wp1 = ak ./ (norm_rsr.^5)';
            wp2 = ak ./ (norm_rsr.^7)';
            B = reshape(r_sub_roh,3,1,[]) .* reshape(r_sub_roh,1,3,[]);
            B = reshape(B,9,[]);
            jp1 = 5*B*(diag(wp2)*r_sub_roh');
            jp2 = repmat(vecI3,1,size(wp1,1))*(diag(wp1)*r_sub_roh');
            jp3 = kron(r_sub_roh*wp1, eye(3)) + kron(eye(3), r_sub_roh*wp1);
            dvecH3Dc_dw = 3*(jp1 - jp2 - jp3);
            dvecHc_dw = kron(E23,E23)*dvecH3Dc_dw*E32;

        otherwise % default (complex)
            rho = par.rho;
            ak = par.ak_comp;
            r_sub_roh = [r' 0]' - rho;
            norm_rsr = vecnorm(r_sub_roh);
            wp1 = ak ./ (norm_rsr.^5)';
            wp2 = ak ./ (norm_rsr.^7)';
            B = reshape(r_sub_roh,3,1,[]) .* reshape(r_sub_roh,1,3,[]);
            B = reshape(B,9,[]);
            jp1 = 5*B*(diag(wp2)*r_sub_roh');
            jp2 = repmat(vecI3,1,size(wp1,1))*(diag(wp1)*r_sub_roh');
            jp3 = kron(r_sub_roh*wp1, eye(3)) + kron(eye(3), r_sub_roh*wp1);
            dvecH3Dc_dw = 3*(jp1 - jp2 - jp3);
            dvecHc_dw = kron(E23,E23)*dvecH3Dc_dw*E32;
    end
end