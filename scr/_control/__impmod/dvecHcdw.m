function dvecHc_dw = dvecHcdw(par, r)
%DVECHCDW Analytical derivative of column-wise vec(Hc), size 4-by-2.
[~, ~, t] = imp_local(par, r(:), 2);
dvecHc_dw = [t(1) t(2); t(2) t(3); t(2) t(3); t(3) t(4)];
end
