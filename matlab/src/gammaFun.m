function outF=gammaFun(Inp,t)

% initial parameters
A1=Inp(1); 
peak1=Inp(2); 
fwhm1=Inp(3); 
m=Inp(4);

% estimate alpha and beta 
alpha1 = ((peak1^2)/(fwhm1^2))*8*log(2);
beta1 = ((fwhm1^2)/peak1)/8*log(2);

% fit
outF=A1*(t/peak1).^alpha1.*exp(-(t-peak1)./beta1)+m;

end