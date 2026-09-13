function results = verify_impmod(runTiming)
%VERIFY_IMPMOD Compare all optimized public functions with the reference.
% Run from any working directory; the previous path, directory and RNG return
% on exit. Uses only MATLAB, no hardware and no optional toolboxes.
if nargin == 0
    runTiming = true;
end
testDir = fileparts(mfilename('fullpath'));
impDir = fileparts(testDir);
controlDir = fileparts(impDir);
refDir = fullfile(controlDir, '__refmod');
oldPath = path;
oldDir = pwd;
oldRng = rng;
cleanup = onCleanup(@() restore_environment(oldPath, oldDir, oldRng)); %#ok<NASGU>
rng(1709, 'twister');
select_model(refDir, refDir, oldPath);
referencePar = parametersGen();
cd(testDir);
select_model(impDir, testDir, oldPath);
optimizedPar = parametersGen();
assert(isequaln(referencePar, optimizedPar), 'Parameter structures differ.');
fprintf('MATLAB %s on %s\n', version, computer);
fprintf('All %d parameter fields match exactly.\n', numel(fieldnames(referencePar)));

% Random workspace states, symmetry/cancellation cases, and workspace edges.
count = 256;
azimuth = 2*pi*rand(1, count);
radius = 0.030*sqrt(rand(1, count));
positions = [radius.*cos(azimuth); radius.*sin(azimuth)];
angles = 2*pi*(2*rand(6, count)-1);
positions = [positions, zeros(2,3)];
angles = [angles, zeros(6,1), pi*ones(6,1), (pi/2)*ones(6,1)];
for k = 0:11
    for theta = [0, pi/2, -pi/2, pi]
        positions(:,end+1) = 0.0343*[cos(k*pi/6); sin(k*pi/6)]; %#ok<AGROW>
        angles(:,end+1) = theta*ones(6,1); %#ok<AGROW>
    end
end
cases = struct('par', {}, 'r', {}, 'theta', {});
for k = 1:size(positions,2)
    cases(k) = struct('par', referencePar, 'r', positions(:,k), 'theta', angles(:,k));
end
% Exercise live parameter changes: no stale caches, fixed six-magnet or
% 144-source assumptions may silently enter the implementation.
p = referencePar;
p.kg = p.kg*1.3;
p.ak_comp = p.ak_comp*0.73;
p.rho = p.rho + [1e-4; -2e-4; 3e-4];
p.rho_k_rad = p.rho_k_rad + [2e-4, -1e-4];
p.phi_rad = p.phi_rad + 0.09;
cases(end+1) = struct('par', p, 'r', positions(:,1), 'theta', angles(:,1));
for n = [1, 3]
    p = referencePar;
    p.n = n;
    p.phi_rad = p.phi_rad(1:n);
    p.rho_k_rad = p.rho_k_rad(1:n,:);
    cases(end+1) = struct('par', p, 'r', positions(:,2), 'theta', angles(1:n,2)); %#ok<AGROW>
end
p = referencePar;
p.rho = p.rho(:,1:2:end);
p.ak_comp = p.ak_comp(1:2:end);
cases(end+1) = struct('par', p, 'r', positions(:,3), 'theta', angles(:,3));

% Include nonzero local z to verify all three h_c components and its norm.
localPoints = [0.03+0.04*rand(1,128); 0.02*(2*rand(1,128)-1); 0.02*(2*rand(1,128)-1)];
names = {'hn_field','Hn','gn_field','Gr','Gth'};
localNames = {'h_c','hc_norm','Hc','dvecHcdw','R'};
select_model(refDir, testDir, oldPath);
reference = cell(numel(cases),5);
for k = 1:numel(cases)
    reference(k,:) = workspace_outputs(cases(k));
end
localReference = cell(size(localPoints,2),5);
for k = 1:size(localPoints,2)
    localReference(k,:) = local_outputs(referencePar, localPoints(:,k), k/7);
end
timingNames = {'h_c','Hc','dvecHcdw','hn_field','Hn','gn_field','gan_linear','g_then_J'};
if runTiming
    fprintf('Benchmarking reference functions...\n');
    referenceTimes = benchmark(referencePar, positions(:,1:128), angles(:,1:128), timingNames);
end

select_model(impDir, testDir, oldPath);
absoluteTolerance = 1e-10;
relativeTolerance = 2e-10;
metrics = zeros(10,3); % max absolute norm, max norm-relative error, max error / max(1,norm)
for k = 1:numel(cases)
    actual = workspace_outputs(cases(k));
    for j = 1:5
        metrics(j,:) = max(metrics(j,:), compare_output(actual{j}, reference{k,j}, ...
            absoluteTolerance, relativeTolerance, sprintf('%s case %d', names{j}, k)));
    end
    [g, Gr, Gth] = gn_field(cases(k).par, cases(k).r, cases(k).theta);
    compare_output(g, actual{3}, 0, 0, 'fused g');
    compare_output(Gr, actual{4}, 0, 0, 'fused Gr');
    compare_output(Gth, actual{5}, 0, 0, 'fused Gth');
end
for k = 1:size(localPoints,2)
    actual = local_outputs(optimizedPar, localPoints(:,k), k/7);
    for j = 1:5
        metrics(5+j,:) = max(metrics(5+j,:), compare_output(actual{j}, localReference{k,j}, ...
            absoluteTolerance, relativeTolerance, sprintf('%s case %d', localNames{j}, k)));
    end
end

% Independent central differences verify the new analytic Jacobians.
fdError = zeros(1,2);
for k = 1:16
    r = positions(:,k);
    th = angles(:,k);
    [Gr, Gth] = gan_linear(optimizedPar, r, th);
    nr = zeros(2,2);
    nt = zeros(2,6);
    for j = 1:2
        dr = zeros(2,1); dr(j) = 1e-7;
        nr(:,j) = (gn_field(optimizedPar,r+dr,th)-gn_field(optimizedPar,r-dr,th))/(2e-7);
    end
    for j = 1:6
        dt = zeros(6,1); dt(j) = 1e-6;
        nt(:,j) = (gn_field(optimizedPar,r,th+dt)-gn_field(optimizedPar,r,th-dt))/(2e-6);
    end
    m1 = compare_output(Gr,nr,1e-5,2e-5,'finite-difference Gr');
    m2 = compare_output(Gth,nt,1e-5,2e-5,'finite-difference Gth');
    fdError = max(fdError,[m1(2) m2(2)]);
end
bad = optimizedPar;
bad.model_sel = 1;
assert_rejects(@() h_c(bad,localPoints(:,1)));
assert_rejects(@() Hc(bad,localPoints(1:2,1)));
assert_rejects(@() dvecHcdw(bad,localPoints(1:2,1)));
assert_rejects(@() hn_field(bad,positions(:,1),angles(:,1)));
assert_rejects(@() Hn(bad,positions(:,1),angles(:,1)));
assert_rejects(@() gn_field(bad,positions(:,1),angles(:,1)));
assert_rejects(@() gan_linear(bad,positions(:,1),angles(:,1)));

allNames = [names localNames];
fprintf('\nPASS: %d workspace cases, %d local cases, 16 finite-difference cases.\n', ...
    numel(cases),size(localPoints,2));
fprintf('%-14s %15s %15s %15s\n','Output','Max abs norm','Max relative','Max scaled');
for j = 1:numel(allNames)
    fprintf('%-14s %15.6g %15.6g %15.6g\n',allNames{j},metrics(j,:));
end
fprintf('Finite-difference relative errors: Gr %.6g, Gth %.6g\n',fdError);
results = struct('workspaceCases',numel(cases),'localCases',size(localPoints,2), ...
    'names',{allNames},'errors',metrics,'finiteDifferenceRelativeErrors',fdError, ...
    'absoluteTolerance',absoluteTolerance,'relativeTolerance',relativeTolerance);
if runTiming
    fprintf('\nBenchmarking optimized functions...\n');
    optimizedTimes = benchmark(optimizedPar,positions(:,1:128),angles(:,1:128),timingNames);
    fusedTimes = benchmark(optimizedPar,positions(:,1:128),angles(:,1:128),{'fused'});
    fprintf('\nMedian of 7 warmed batches, 128 distinct states per batch; milliseconds/call.\n');
    fprintf('%-14s %12s %12s %12s\n','Function','Reference','Optimized','Speedup');
    refMedian = median(referenceTimes,2);
    impMedian = median(optimizedTimes,2);
    for j = 1:numel(timingNames)
        fprintf('%-14s %12.6f %12.6f %11.2fx\n',timingNames{j}, ...
            1e3*refMedian(j),1e3*impMedian(j),refMedian(j)/impMedian(j));
    end
    fprintf('Fused [g,Gr,Gth]: %.6f ms/call; %.2fx versus reference separate calls.\n', ...
        1e3*median(fusedTimes),refMedian(end)/median(fusedTimes));
    results.timingNames = timingNames;
    results.referenceSeconds = referenceTimes;
    results.optimizedSeconds = optimizedTimes;
    results.fusedSeconds = fusedTimes;
end
end

function values = workspace_outputs(c)
[a,b] = gan_linear(c.par,c.r,c.theta);
values = {hn_field(c.par,c.r,c.theta),Hn(c.par,c.r,c.theta),gn_field(c.par,c.r,c.theta),a,b};
end

function values = local_outputs(p,w,angle)
[h,hn] = h_c(p,w);
values = {h,hn,Hc(p,w(1:2)),dvecHcdw(p,w(1:2)),R(angle)};
end

function m = compare_output(actual,expected,atol,rtol,label)
assert(isequal(size(actual),size(expected)), '%s: output shape differs.',label);
assert(all(isfinite(actual(:))) && all(isfinite(expected(:))), '%s: nonfinite output.',label);
delta = norm(actual(:)-expected(:));
scale = norm(expected(:));
assert(delta <= atol+rtol*scale, '%s mismatch: norm error %.17g, scale %.17g.',label,delta,scale);
m = [delta,delta/max(scale,eps),delta/max(scale,1)];
end

function assert_rejects(f)
try
    f();
catch exception
    assert(strcmp(exception.identifier,'SixMag:ComplexModelOnly'),'Unexpected error for unsupported model.');
    return
end
error('Unsupported model did not fail explicitly.');
end

function seconds = benchmark(p,rr,tt,names)
seconds = zeros(numel(names),7);
for j = 1:numel(names)
    for warm = 1:3
        run_batch(names{j},p,rr,tt);
    end
    for repeat = 1:7
        timer = tic;
        checksum = run_batch(names{j},p,rr,tt);
        seconds(j,repeat) = toc(timer)/size(rr,2);
        assert(isfinite(checksum),'Nonfinite benchmark checksum.');
    end
end
end

function checksum = run_batch(task,p,rr,tt)
checksum = 0;
% Dispatch once per batch; the measured loop still includes ordinary MATLAB
% call/indexing overhead, identically for reference and optimized functions.
switch task
    case 'h_c'
        for k=1:size(rr,2)
            a=h_c(p,[rr(:,k)+[0.045;0];0]); checksum=checksum+a(1);
        end
    case 'Hc'
        for k=1:size(rr,2)
            a=Hc(p,rr(:,k)+[0.045;0]); checksum=checksum+a(1);
        end
    case 'dvecHcdw'
        for k=1:size(rr,2)
            a=dvecHcdw(p,rr(:,k)+[0.045;0]); checksum=checksum+a(1);
        end
    case 'hn_field'
        for k=1:size(rr,2)
            a=hn_field(p,rr(:,k),tt(:,k)); checksum=checksum+a(1);
        end
    case 'Hn'
        for k=1:size(rr,2)
            a=Hn(p,rr(:,k),tt(:,k)); checksum=checksum+a(1);
        end
    case 'gn_field'
        for k=1:size(rr,2)
            a=gn_field(p,rr(:,k),tt(:,k)); checksum=checksum+a(1);
        end
    case 'gan_linear'
        for k=1:size(rr,2)
            [a,b]=gan_linear(p,rr(:,k),tt(:,k)); checksum=checksum+a(1)+b(1);
        end
    case 'g_then_J'
        for k=1:size(rr,2)
            g=gn_field(p,rr(:,k),tt(:,k));
            [a,b]=gan_linear(p,rr(:,k),tt(:,k)); checksum=checksum+g(1)+a(1)+b(1);
        end
    case 'fused'
        for k=1:size(rr,2)
            [g,a,b]=gn_field(p,rr(:,k),tt(:,k)); checksum=checksum+g(1)+a(1)+b(1);
        end
end
end

function select_model(folder,workDir,basePath)
path(basePath);
addpath(folder,'-begin');
cd(workDir);
clear parametersGen R h_c Hc dvecHcdw hn_field Hn gn_field gan_linear imp_local imp_evaluate
rehash;
names = {'parametersGen','R','h_c','Hc','dvecHcdw','hn_field','Hn','gn_field','gan_linear'};
for k = 1:numel(names)
    assert(strcmpi(fileparts(which(names{k})),folder),'Wrong implementation selected: %s.',names{k});
end
end

function restore_environment(savedPath,savedDir,savedRng)
cd(savedDir);
path(savedPath);
rng(savedRng);
clear parametersGen R h_c Hc dvecHcdw hn_field Hn gn_field gan_linear imp_local imp_evaluate
end
