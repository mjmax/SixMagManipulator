function rot_mat = R(phi)
%R Global-to-local rotation; phi is in radians.
c = cos(phi);
s = sin(phi);
rot_mat = [c s; -s c];
end
