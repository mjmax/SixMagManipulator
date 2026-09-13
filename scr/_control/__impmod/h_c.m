function [hc_field, hc_norm] = h_c(par, pk)
%H_C Complex-model field at a 3-by-1 local point, including its z component.
% The optional norm is evaluated only when requested.
if par.model_sel ~= 0
    error('SixMag:ComplexModelOnly', 'The optimized functions support model_sel = 0 only.');
end
q = pk(:) - par.rho;
inverse2 = 1 ./ sum(q .* q, 1);
weights = par.ak_comp(:).' .* inverse2 .* sqrt(inverse2);
hc_field = q * weights.';
if nargout > 1
    hc_norm = norm(hc_field);
end
end
