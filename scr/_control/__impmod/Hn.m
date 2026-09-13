function jacobn = Hn(par, r, theta_v)
%HN Analytical position Jacobian of the scaled combined planar field.
[~, jacobn] = imp_evaluate(par, r, theta_v, 1);
end
