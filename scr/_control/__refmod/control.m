function u = control(par,r,theta)
	k=4000;
	yd = -k*r;
	%gf = gn_field(par,r,theta);
    	[Gr,Gth] = gan_linear(par,[0 0]',[0 0 0 0 0 0]');
    	GthDag = pinv(Gth);
	u = GthDag*yd;
	%u = theta + GthDag*(yd - gf);
end