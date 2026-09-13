function [Gr, Gth] = gan_linear(par, r, theta_v)
%GAN_LINEAR Analytical acceleration Jacobians, compatible with the reference.
[~, ~, Gr, Gth] = imp_evaluate(par, r, theta_v, 2);
end
