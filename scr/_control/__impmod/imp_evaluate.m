function [h, H, Gr, Gth] = imp_evaluate(par, r, theta_v, order)
%IMP_EVALUATE Shared analytical workspace helper for the model wrappers.
% order 0: field; order 1: field/gradient; order 2: acceleration derivatives.
% Real double SI inputs, radians, and the reference zero-angle convention.
angles = theta_v(:).' + par.phi_rad(:).';
c = cos(angles);
s = sin(angles);
p = r(:) - par.rho_k_rad.';
wx = c .* p(1,:) + s .* p(2,:);
wy = -s .* p(1,:) + c .* p(2,:);
[f, a, t] = imp_local(par, [wx; wy], order);
scale = sqrt(2 * par.kg);
fx = c .* f(1,:) - s .* f(2,:);
fy = s .* f(1,:) + c .* f(2,:);
h = scale * [sum(fx); sum(fy)];
H = [];
Gr = [];
Gth = [];
if order == 0
    return
end
c2 = c .* c;
s2 = s .* s;
cs = c .* s;
axx = a(1,:);
axy = a(2,:);
ayy = a(3,:);
xx = c2 .* axx - 2 * cs .* axy + s2 .* ayy;
xy = cs .* (axx - ayy) + (c2 - s2) .* axy;
yy = s2 .* axx + 2 * cs .* axy + c2 .* ayy;
H = scale * [sum(xx) sum(xy); sum(xy) sum(yy)];
if order == 1
    return
end
% Rotate the symmetric third-order tensor; four components suffice in 2D.
t1 = t(1,:);
t2 = t(2,:);
t3 = t(3,:);
t4 = t(4,:);
c3 = c2 .* c;
s3 = s2 .* s;
c2s = c2 .* s;
cs2 = c .* s2;
v1 = scale * sum(c3 .* t1 - 3*c2s .* t2 + 3*cs2 .* t3 - s3 .* t4);
v2 = scale * sum(c2s .* t1 + (c3-2*cs2) .* t2 + (s3-2*c2s) .* t3 + cs2 .* t4);
v3 = scale * sum(cs2 .* t1 + (2*c2s-s3) .* t2 + (c3-2*cs2) .* t3 - c2s .* t4);
v4 = scale * sum(s3 .* t1 + 3*cs2 .* t2 + 3*c2s .* t3 + c3 .* t4);
cross = v2*h(1) + v3*h(2);
Gr = H * H + [v1*h(1)+v2*h(2), cross; cross, v3*h(1)+v4*h(2)];
% dw/dtheta = [wy; -wx]. Rotation of the field adds J*f_global.
u = axx .* wy - axy .* wx;
v = axy .* wy - ayy .* wx;
dh = scale * [-fy + c .* u - s .* v; fx + s .* u + c .* v];
% d(R'*A*R)/dtheta = J*A_global - A_global*J + R'*T[dw]*R.
txx = t1 .* wy - t2 .* wx;
txy = t2 .* wy - t3 .* wx;
tyy = t3 .* wy - t4 .* wx;
qxx = scale * (-2*xy + c2 .* txx - 2*cs .* txy + s2 .* tyy);
qxy = scale * (xx-yy + cs .* (txx-tyy) + (c2-s2) .* txy);
qyy = scale * (2*xy + s2 .* txx + 2*cs .* txy + c2 .* tyy);
Gth = H * dh + [h(1)*qxx+h(2)*qxy; h(1)*qxy+h(2)*qyy];
end
