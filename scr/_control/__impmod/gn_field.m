function [g, Gr, Gth] = gn_field(par, r, theta_v)
%GN_FIELD Magnetic acceleration; existing single-output calls are unchanged.
% Optional [g, Gr, Gth] returns all three quantities in one shared evaluation.
if nargout > 1
    [h, H, Gr, Gth] = imp_evaluate(par, r, theta_v, 2);
else
    [h, H] = imp_evaluate(par, r, theta_v, 1);
end
g = H * h;
end
