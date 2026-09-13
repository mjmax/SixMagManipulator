function H_c = Hc(par, r)
%HC Analytical planar field Jacobian at the local point [r; 0].
[~, a] = imp_local(par, r(:), 1);
H_c = [a(1) a(2); a(2) a(3)];
end
