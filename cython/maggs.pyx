
def setParams(bjerrum, fMass, mesh):
    if bjerrum<0.0:
        raise ValueError("bjerrum length must be positive.")
    if fMass<0.0:
        raise ValueError("mass of the field must be positive.")
    if mesh<0:
        raise ValueError("mesh must be positive.")

    maggs_set_parameters(bjerrum, fMass, mesh)