function [Gr, Gth] = gan_linear(par,r,theta_v)
    phi_rad_k = par.phi_rad;
    rho_k = par.rho_k_rad;
    diff_pos = (r - rho_k');
    I2 = eye(2,2);
    In = eye(par.n,par.n);
    dh_dth = zeros(2,par.n);
    dvecH_dth = zeros(4,par.n);
    dvecH_dr = zeros(4,2);
    H = Hn(par,r,theta_v);
    h = hn_field(par,r,theta_v);
    for i = 1:par.n
        ang = theta_v(i) + phi_rad_k(i);
        ei = In(:,i);
        dvecRi_dth =[-sin(ang);-cos(ang);cos(ang);-sin(ang)];
        dvecRiT_dth =[-sin(ang);cos(ang);-cos(ang);-sin(ang)]; 
        dvecR_dth = dvecRi_dth*ei';
        dvecRT_dth = dvecRiT_dth*ei';
    
        Ri = R(ang);                            % compute once
        pi = diff_pos(:,i);
        pki = Ri * pi;                          % rotated vector
    
        Hci = sqrt(2*par.kg)*Hc(par, pki);                     % compute once
        hci = sqrt(2*par.kg)*[1 0 0;0 1 0]*h_c(par,[pki;0]);
        dvecHc_dw = sqrt(2*par.kg)*dvecHcdw(par,pki);
    
        dh_dth = dh_dth + ((Ri' * Hci * kron(I2,pi') * dvecRT_dth) + (kron(I2,hci') * dvecR_dth));
        Pk = kron((Ri'*Hci'),I2) * dvecRT_dth;
        Qk = kron(I2,(Ri'*Hci)) * dvecR_dth;
        kron_RiT_RiT = kron(Ri',Ri');
        dvecH_dth = dvecH_dth + Pk + Qk + (kron_RiT_RiT * dvecHc_dw * (kron(I2,pi') * dvecRT_dth));
        dvecH_dr = dvecH_dr + (kron_RiT_RiT * dvecHc_dw * Ri);
    end

    Gth = H*dh_dth + (kron(I2,h')*dvecH_dth);
    Gr = H*H + (kron(I2,h')*dvecH_dr);
end