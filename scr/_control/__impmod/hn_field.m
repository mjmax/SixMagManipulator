function hn_f = hn_field(par, r, theta_v)
%HN_FIELD Scaled combined planar field, using the reference conventions.
hn_f = imp_evaluate(par, r, theta_v, 0);
end
