function [h, a, t] = imp_local(par, w, order)
%IMP_LOCAL Shared helper: batch planar points against all fitted sources.
% h: [hx;hy], a: [Hxx;Hxy;Hyy], t: [Txxx;Txxy;Txyy;Tyyy].
% Arrays have one column per point. Source heights remain in every distance.
% No persistent parameter cache: changes made by the simulator take effect.
if par.model_sel ~= 0
    error('SixMag:ComplexModelOnly', 'The optimized functions support model_sel = 0 only.');
end
x = w(1,:) - par.rho(1,:).';
y = w(2,:) - par.rho(2,:).';
z = par.rho(3,:).';
x2 = x .* x;
y2 = y .* y;
inverse2 = 1 ./ (x2 + y2 + z .* z);
weight3 = par.ak_comp(:) .* inverse2 .* sqrt(inverse2);
h = [sum(x .* weight3, 1); sum(y .* weight3, 1)];
a = [];
t = [];
if order == 0
    return
end
weight5 = weight3 .* inverse2;
xy = x .* y;
base = sum(weight3, 1);
a = [base - 3 * sum(x2 .* weight5, 1); ...
     -3 * sum(xy .* weight5, 1); ...
     base - 3 * sum(y2 .* weight5, 1)];
if order == 1
    return
end
weight7 = weight5 .* inverse2;
sx = sum(x .* weight5, 1);
sy = sum(y .* weight5, 1);
t = [15 * sum(x2 .* x .* weight7, 1) - 9 * sx; ...
     15 * sum(x2 .* y .* weight7, 1) - 3 * sy; ...
     15 * sum(y2 .* x .* weight7, 1) - 3 * sx; ...
     15 * sum(y2 .* y .* weight7, 1) - 9 * sy];
end
