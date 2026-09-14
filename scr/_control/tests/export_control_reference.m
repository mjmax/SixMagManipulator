function export_control_reference()
%EXPORT_CONTROL_REFERENCE Compare against control.m without modifying it.
% An earlier supplied copy had "pr" instead of "par"; if present, repair only
% a temporary renamed copy. All other executable statements are preserved.
testDir=fileparts(mfilename('fullpath'));
controlDir=fileparts(testDir);
refDir=fullfile(controlDir,'__refmod');
buildDir=fullfile(controlDir,'build');
if ~isfolder(buildDir), mkdir(buildDir); end
scratch=tempname(buildDir);
assert(strcmp(fileparts(scratch),buildDir));
mkdir(scratch);
copyFile=fullfile(scratch,'control_reference_for_test.m');
savedPath=path; savedDir=pwd; savedRng=rng;
cleanup=onCleanup(@() restore(savedPath,savedDir,savedRng,copyFile,scratch)); %#ok<NASGU>
source=fileread(fullfile(refDir,'control.m'));
assert(contains(source,'function u = control('),'Unexpected reference signature.');
source=strrep(source,'function u = control(','function u = control_reference_for_test(');
repairedTypo=contains(source,'gan_linear(pr,');
if repairedTypo
    assert(count(source,'gan_linear(pr,')==1,'Unexpected repeated pr expression.');
    source=strrep(source,'gan_linear(pr,','gan_linear(par,');
    fprintf('Corrected pr -> par in isolated test copy only.\n');
end
fid=fopen(copyFile,'w'); assert(fid>=0);
fprintf(fid,'%s',source); fclose(fid);
addpath(refDir,'-begin'); addpath(scratch,'-begin'); cd(refDir);
clear parametersGen R h_c Hc dvecHcdw hn_field Hn gn_field gan_linear control_reference_for_test
names={'parametersGen','R','h_c','Hc','dvecHcdw','hn_field','Hn','gn_field','gan_linear'};
for k=1:numel(names)
    assert(strcmpi(fileparts(which(names{k})),refDir),'Wrong reference implementation.');
end
assert(strcmpi(fileparts(which('control_reference_for_test')),scratch));
p=parametersGen();
rng(4109,'twister');
angle=2*pi*rand(1,256); radius=0.030*sqrt(rand(1,256));
r=[radius.*cos(angle);radius.*sin(angle)];
r=[r,zeros(2,1),[.001,0,-.001,0;.0,.001,0,-.001], ...
    [.030,0,-.030,0;0,.030,0,-.030]];
theta=4*pi*rand(6,size(r,2))-2*pi;
fid=fopen(fullfile(testDir,'control_reference_cases.csv'),'w'); assert(fid>=0);
closer=onCleanup(@() fclose(fid));
fprintf(fid,'# __refmod/control.m; renamed test copy; legacy pr->par repair=%d; seed 4109; all SI\n',repairedTypo);
fprintf(fid,'# sigma_v=%.17g\n',p.sigma_v);
fprintf(fid,'# r[2],theta[6],u[6]; k=4000; r0=0;theta0=0; no saturation in reference\n');
for k=1:size(r,2)
    u=control_reference_for_test(p,r(:,k),theta(:,k));
    assert(isequal(size(u),[6,1]) && all(isfinite(u)));
    row=[r(:,k);theta(:,k);u];
    fprintf(fid,'%.17g,',row(1:end-1)); fprintf(fid,'%.17g\n',row(end));
end
clear closer
fprintf('Exported %d original-controller comparison cases.\n',size(r,2));
end
function restore(p,d,r,copyFile,scratch)
cd(d);path(p);rng(r);
clear parametersGen R h_c Hc dvecHcdw hn_field Hn gn_field gan_linear control_reference_for_test
if isfile(copyFile), delete(copyFile); end
if isfolder(scratch), rmdir(scratch); end
end
